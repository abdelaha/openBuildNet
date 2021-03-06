/* -*- mode: C++; indent-tabs-mode: nil; -*- */
/** \file
 * \brief YARP port classes for the C++ node interface.
 *
 * Implement the communication interface with YARP port for a C++ node.
 *
 * This file is part of the openBuildNet simulation framework
 * (OBN-Sim) developed at EPFL.
 *
 * \author Truong X. Nghiem (xuan.nghiem@epfl.ch)
 */

#ifndef OBNNODE_YARPPORT_H_
#define OBNNODE_YARPPORT_H_

#include <type_traits>
#include <string>
#include <queue>        // std::queue
#include <algorithm>    // std::copy_n, etc.

#include <yarp/os/all.h>
#include <sharedqueue_yarp.h>

#include <Eigen/Core>
#include <obnsim_io.pb.h>

#include <obnnode_basic.h>
#include <obnnode_yarpportbase.h>
#include <obnnode_yarpnode.h>

#include <obnnode_exceptions.h>

namespace OBNnode {
    /** \brief Template class for an input port with specific type.
     
     This template class defines an input port with a specific fixed type (e.g. scalar, vector, matrix).
     Specializations are used to define the classes for each type.
     The template has the following signature:
     template <FORMAT, DATATYPE, STRICT> class InputPort;
     where:
     - FORMAT specifies the message format and is one of: OBN_PB for ProtoBuf for a fixed type, OBN_PB_USER for any ProtoBuf message format (which will be specified by DATATYPE), or OBN_BIN for raw binary data (user-defined format).
     - DATATYPE specifies the type of data, depending on FORMAT
     + If FORMAT is OBN_PB, DATATYPE can be:
     . bool, int32_t, int64_t, uint32_t, uint64_t, double, float: for scalars.
     . obn_vector<t> where t is one of the above types: a variable-length vector of elements of such type.
     . obn_vector_fixed<t, N> where t is one of the above type and N is a constant positive integer: a fixed-length vector of elements of such type. The data is statically allocated, hence more efficient. Incoming data will be checked and an error will be raised if its length is different than N.
     . obn_matrix<t> similar to obn_vector<t> but for a 2-D matrix.
     . obn_matrix_fixed<t, M, N> similar to obn_vector_fixed<t, N> but for a matrix of fixed numbers of rows (M) and columns (N).
     + If FORMAT is OBN_PB_USER then DATATYPE must be a ProtoBuf class generated by protoc. This may not be checked at compile time, but certain necessary methods of a ProtoBuf class for encoding and decoding data must be present. The data read from this port will be an object of this ProtoBuf class. The user is responsible for extracting values from the object.
     + If FORMAT is OBN_BIN then DATATYPE is irrelevant because the data read from this input port will be a binary string.
     - STRICT is a boolean value: if it is false (default), the input port is nonstrict, which means that any new incoming data will immediately replace the current datum (even if it has not been accessed); if it is true, the input port is strict, i.e. new incoming messages will not replace past messages but be queued to be accessed later.
     */
    template <typename F, typename D, const bool S=false>
    class YarpInput;
    
    
    /** \brief Template class for an Yarp output port with specific type.
     
     This template class defines an Yarp output port with a specific fixed type (e.g. scalar, vector, matrix).
     Specializations are used to define the classes for each type.
     The template has the following signature:
     template <FORMAT, DATATYPE> class YarpOutput;
     where:
     - FORMAT specifies the message format and is one of: OBN_PB for ProtoBuf for a fixed type, OBN_PB_USER for any ProtoBuf message format (which will be specified by DATATYPE), or OBN_BIN for raw binary data (user-defined format).
     - DATATYPE specifies the type of data, depending on FORMAT
     + If FORMAT is OBN_PB, DATATYPE can be:
     . bool, int32_t, int64_t, uint32_t, uint64_t, double, float: for scalars.
     . obn_vector<t> where t is one of the above types: a variable-length vector of elements of such type.
     . obn_vector_fixed<t, N> where t is one of the above type and N is a constant positive integer: a fixed-length vector of elements of such type. The data is statically allocated, hence more efficient.
     . obn_matrix<t> similar to obn_vector<t> but for a 2-D matrix.
     . obn_matrix_fixed<t, M, N> similar to obn_vector_fixed<t, N> but for a matrix of fixed numbers of rows (M) and columns (N).
     + If FORMAT is OBN_PB_USER then DATATYPE must be a ProtoBuf class generated by protoc. This may not be checked at compile time, but certain necessary methods of a ProtoBuf class for encoding and decoding data must be present. The data assigned to this port will be an object of this ProtoBuf class. The user is responsible for populating the object with appropriate values.
     + If FORMAT is OBN_BIN then DATATYPE is irrelevant because the data written to this output port will be a binary string.
     */
    template <typename F, typename D>
    class YarpOutput;
    
    /** Implementation of YarpInput for fixed data type encoded with ProtoBuf (OBN_PB), non-strict reading. */
    template <typename D>
    class YarpInput<OBN_PB, D, false>: public YarpPortBase,
    protected yarp::os::BufferedPort< YARPMsgPB<OBNSimIOMsg::IOAck, typename OBN_DATA_TYPE_CLASS<D>::PB_message_class> >
    {
        typedef OBN_DATA_TYPE_CLASS<D> _obn_data_type_class;
        typedef YARPMsgPB<OBNSimIOMsg::IOAck, typename OBN_DATA_TYPE_CLASS<D>::PB_message_class> _port_content_type;
        
    public:
        typedef typename _obn_data_type_class::input_data_type ValueType;

    private:
        typename _obn_data_type_class::input_data_container _cur_value;    ///< The typed value stored in this port
        bool _pending_value;    ///< If a new value is pending (hasn't been read)
        
        /** The ProtoBuf message object to receive the data.
         This should be a permanent variable (a class member) rather than a temporary variable (in a function)
         because some implementations directly use the data stored in this message, rather than copying the data over.
         If a temporary message variable is used, in those cases, the program may crash (invalid access error). */
        typename _obn_data_type_class::PB_message_class _PBMessage;
        
        mutable yarp::os::Mutex _valueMutex;    ///< Mutex for accessing the value
        
        virtual void onRead(_port_content_type& b) override {
            // printf("Callback[%s]\n", getName().c_str());
            
            // This managed input port does not generate events in the main thread
            // It simply saves the value in the message to the value
            
            try {
                // Parse the ProtoBuf message
                if (!b.getMessage(_PBMessage)) {
                    // Error while parsing the raw message
                    throw OBNnode::inputport_error(this, OBNnode::inputport_error::ERR_RAWMSG);
                }
                
                // Read from the ProtoBuf message to the value
                _valueMutex.lock();
                bool result = OBN_DATA_TYPE_CLASS<D>::readPBMessage(_cur_value, _PBMessage);
                if (result) {
                    _pending_value = true;
                }
                _valueMutex.unlock();
                
                if (result) {
                    triggerMsgRcvCallback();    // Trigger the Message Received Event Callback
                } else {
                    // Error while reading the value, e.g. sizes don't match
                    throw OBNnode::inputport_error(this, OBNnode::inputport_error::ERR_READVALUE);
                }
                
            } catch (...) {
                // Catch everything and pass it to the main thread
                m_node->postExceptionEvent(std::current_exception());
            }
        }
    
    public:
        YarpInput(const std::string& _name): YarpPortBase(_name), _pending_value(false) {
            
        }
        
        /** Get the current value of the port. If no message has been received, the value is undefined.
         The value is copied out, which may be inefficient for large data (e.g. a large vector or matrix).
         */
        ValueType operator() () {
            yarp::os::LockGuard mlock(_valueMutex);
            _pending_value = false; // the value has been read
            return _cur_value.v;
        }

        ValueType get() {
            yarp::os::LockGuard mlock(_valueMutex);
            _pending_value = false; // the value has been read
            return _cur_value.v;
        }
        
        typedef OBNnode::LockedAccess<typename _obn_data_type_class::input_data_container::data_type, yarp::os::Mutex> LockedAccess;
        
        /** Returns a thread-safe direct access to the value of the port. */
        LockedAccess lock_and_get() {
            _pending_value = false; // the value has been read
            return LockedAccess(&_cur_value.v, &_valueMutex);
        }
        
        /** Check if there is a pending input value (that hasn't been read). */
        virtual bool isValuePending() const override {
            return _pending_value;
        }
        
        
    protected:
        virtual yarp::os::Contactable& getYarpPort() override {
            return *this;
        }
        
        virtual const yarp::os::Contactable& getYarpPort() const override {
            return *this;
        }
        
        virtual bool configure() override {
            // Turn on callback
            this->useCallback();
            return true;
        }
    };

    
    /** Implementation of YarpInput for custom ProtoBuf messages, non-strict reading. */
    template <typename PBCLS>
    class YarpInput<OBN_PB_USER, PBCLS, false>: public YarpPortBase,
    protected yarp::os::BufferedPort< YARPMsgPB<OBNSimIOMsg::IOAck, PBCLS> >
    {
        typedef YARPMsgPB<OBNSimIOMsg::IOAck, PBCLS> _port_content_type;

        PBCLS _cur_message;    ///< The current ProtoBuf data message stored in this port
        bool _pending_value;    ///< If a new value is pending (hasn't been read)
        
        mutable yarp::os::Mutex _valueMutex;    ///< Mutex for accessing the value
        
        virtual void onRead(_port_content_type& b) {
            // printf("Callback[%s]\n", getName().c_str());
            
            // This managed input port does not generate events in the main thread
            // It simply saves the value in the message to the value
            
            try {
                // Parse the ProtoBuf message
                _valueMutex.lock();
                bool result = b.getMessage(_cur_message);
                if (result) {
                    _pending_value = true;
                }
                _valueMutex.unlock();
                
                if (result) {
                    triggerMsgRcvCallback();    // Trigger the Message Received Event Callback
                } else {
                    // Error while parsing the raw message
                    throw OBNnode::inputport_error(this, OBNnode::inputport_error::ERR_RAWMSG);
                }
            } catch (...) {
                // Catch everything and pass it to the main thread
                m_node->postExceptionEvent(std::current_exception());
            }
        }
        
    public:
        YarpInput(const std::string& _name): YarpPortBase(_name), _pending_value(false) {
            
        }
        
        /** Returns a copy of the current message.
         To get direct access to the current message (without copying) see lock_and_get().
         */
        PBCLS get() {
            yarp::os::LockGuard mlock(_valueMutex);
            _pending_value = false; // the value has been read
            return _cur_message;
        }
        
        typedef OBNnode::LockedAccess<PBCLS, yarp::os::Mutex> LockedAccess;

        /** Returns a thread-safe direct access to the value of the port. */
        LockedAccess lock_and_get() {
            _pending_value = false;  // the value has been read
            return LockedAccess(&_cur_message, &_valueMutex);
        }
        
        /** Check if there is a pending input value (that hasn't been read). */
        virtual bool isValuePending() const override {
            return _pending_value;
        }
        
        
    protected:
        virtual yarp::os::Contactable& getYarpPort() override {
            return *this;
        }
        
        virtual const yarp::os::Contactable& getYarpPort() const override {
            return *this;
        }
        
        virtual bool configure() {
            // Turn on callback
            this->useCallback();
            return true;
        }
    };
    
    
    /** Implementation of YarpInput for binary data, non-strict reading. */
    template <typename D>
    class YarpInput<OBN_BIN, D, false>: public YarpPortBase,
    protected yarp::os::BufferedPort<YARPMsgBin>
    {
        typedef YARPMsgBin _port_content_type;
        
        std::string _cur_message;    ///< The current binary data message stored in this port
        bool _pending_value;    ///< If a new value is pending (hasn't been read)
        mutable yarp::os::Mutex _valueMutex;    ///< Mutex for accessing the value
        
        virtual void onRead(_port_content_type& b) {
            // printf("Callback[%s]\n", getName().c_str());
            
            // This managed input port does not generate events in the main thread
            // It simply saves the value in the message to the value
            
            // Copy the binary data to _cur_message
            _valueMutex.lock();
            _cur_message.assign(b.getBinaryData(), b.getBinaryDataSize());
            _pending_value = true;
            _valueMutex.unlock();
            
            triggerMsgRcvCallback();    // Trigger the Message Received Event Callback
        }
        
    public:
        YarpInput(const std::string& _name): YarpPortBase(_name), _pending_value(false) {
            
        }
        
        /** Returns a copy of the current binary content, as a string.
         */
        std::string get() {
            yarp::os::LockGuard mlock(_valueMutex);
            _pending_value = false; // the value has been read
            return _cur_message;
        }
        
        typedef OBNnode::LockedAccess<std::string, yarp::os::Mutex> LockedAccess;
        
        /** Returns a thread-safe direct access to the value of the port. */
        LockedAccess lock_and_get() {
            _pending_value = false;  // the value has been read
            return LockedAccess(&_cur_message, &_valueMutex);
        }
        
        /** Check if there is a pending input value (that hasn't been read). */
        virtual bool isValuePending() const override {
            return _pending_value;
        }
        
        
    protected:
        virtual yarp::os::Contactable& getYarpPort() override {
            return *this;
        }
        
        virtual const yarp::os::Contactable& getYarpPort() const override {
            return *this;
        }
        
        virtual bool configure() {
            // Turn on callback
            this->useCallback();
            return true;
        }
    };
    
    
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //          Implementations of YarpInput for strict reading ports
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
   
    
    
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //          Implementations of YarpOutput
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    
    /** Implementation of YarpOutput for fixed data type encoded with ProtoBuf (OBN_PB).
     This class of YarpOutput is not thread-safe because usually it's accessed in the main thread only.
     */
    template <typename D>
    class YarpOutput<OBN_PB, D>: public YarpOutputPortBase,
    protected yarp::os::BufferedPort< YARPMsgPB<typename OBN_DATA_TYPE_CLASS<D>::PB_message_class, OBNSimIOMsg::IOAck> >
    {
        typedef OBN_DATA_TYPE_CLASS<D> _obn_data_type_class;
        typedef YARPMsgPB<typename OBN_DATA_TYPE_CLASS<D>::PB_message_class, OBNSimIOMsg::IOAck> _port_content_type;

    public:
        typedef typename _obn_data_type_class::output_data_type ValueType;
        
    private:
        ValueType _cur_value;    ///< The value stored in this port
        typename _obn_data_type_class::PB_message_class _PBMessage;   ///< The ProtoBuf message object to format the data
        
    public:
        
        YarpOutput(const std::string& _name): YarpOutputPortBase(_name) {
        }
        
        /** Get the current (read-only) value of the port.
         The value is copied out, which may be inefficient for large data (e.g. a large vector or matrix).
         */
        ValueType operator() () const {
            return _cur_value;
        }
        
        /** Directly access the value stored in this port; can change it (so it'll be marked as changed).
         If the value is a fixed-size Eigen vector/matrix and is going to be accessed many times, it will be a good idea to copy it to a local variable because the internal value variable in the port is not aligned for vectorization.
         Once all computations are done, the new value can be assigned to the port using either this operator or the assignment operator.
         */
        ValueType& operator* () {
            m_isChanged = true;
            return _cur_value;
        }
        
        /** Assign new value to the port. */
        ValueType& operator= (ValueType && rhs) {
            _cur_value = std::move(rhs);
            m_isChanged = true;
            return _cur_value;
        }
        
        /** Assign new value to the port. */
        ValueType& operator= (const ValueType & rhs) {
            _cur_value = rhs;
            m_isChanged = true;
            return _cur_value;
        }
        
        
        /** Send data synchronously */
        virtual void sendSync() override {
            try {
                // Convert data to message
                OBN_DATA_TYPE_CLASS<D>::writePBMessage(_cur_value, _PBMessage);
                
                // Prepare the Yarp message to send
                _port_content_type & output = this->prepare();
                if (!output.setMessage(_PBMessage)) {
                    // Error while serializing the raw message
                    throw OBNnode::outputport_error(this, OBNnode::outputport_error::ERR_SENDMSG);
                }
                
                // Actually send the message
                this->writeStrict();
                m_isChanged = false;
            }
            catch (...) {
                // Catch everything and pass it to the main thread
                m_node->postExceptionEvent(std::current_exception());
            }
        }
    
    protected:
        virtual yarp::os::Contactable& getYarpPort() override {
            return *this;
        }
        
        virtual const yarp::os::Contactable& getYarpPort() const override {
            return *this;
        }
    };
    
    
    /** Implementation of YarpOutput for custom ProtoBuf data message (OBN_PB_USER).
     This class of YarpOutput is not thread-safe because usually it's accessed in the main thread only.
     */
    template <typename PBCLS>
    class YarpOutput<OBN_PB_USER, PBCLS>: public YarpOutputPortBase,
    protected yarp::os::BufferedPort< YARPMsgPB<PBCLS, OBNSimIOMsg::IOAck> >
    {
        typedef YARPMsgPB<PBCLS, OBNSimIOMsg::IOAck> _port_content_type;
        PBCLS _cur_message;    ///< The ProtoBuf message stored in this port
        
    public:
        YarpOutput(const std::string& _name): YarpOutputPortBase(_name) {
        }
        
        /** Directly access the ProtoBuf message stored in this port; can change it (so it'll be marked as changed). */
        PBCLS& message() {
            m_isChanged = true;
            return _cur_message;
        }
        
        /** Set the message content. */
        PBCLS& setMessage (const PBCLS& m) {
            m_isChanged = true;
            return (_cur_message = m);
        }
        
        /** Assign a new message to the content of the port. */
        PBCLS& operator= (const PBCLS& m) {
            return setMessage(m);
        }
        
        /** Send data synchronously */
        virtual void sendSync() override {
            try {
                // Prepare the Yarp message to send
                _port_content_type & output = this->prepare();
                if (!output.setMessage(_cur_message)) {
                    // Error while serializing the raw message
                    throw OBNnode::outputport_error(this, OBNnode::outputport_error::ERR_SENDMSG);
                }
                
                // Actually send the message
                this->writeStrict();
                m_isChanged = false;
            }
            catch (...) {
                m_node->postExceptionEvent(std::current_exception());
            }
        }
        
    protected:
        virtual yarp::os::Contactable& getYarpPort() override {
            return *this;
        }
        
        virtual const yarp::os::Contactable& getYarpPort() const override {
            return *this;
        }
    };

    
    /** Implementation of YarpOutput for binary data message (OBN_BIN).
     This class of YarpOutput is not thread-safe because usually it's accessed in the main thread only.
     */
    template <typename D>
    class YarpOutput<OBN_BIN, D>: public YarpOutputPortBase,
    protected yarp::os::BufferedPort< YARPMsgBin >
    {
        typedef YARPMsgBin _port_content_type;
        std::string _cur_message;    ///< The binary data message stored in this port
        
    public:
        YarpOutput(const std::string& _name): YarpOutputPortBase(_name) {
        }
        
        /** Directly access the ProtoBuf message stored in this port; can change it (so it'll be marked as changed). */
        std::string& message() {
            m_isChanged = true;
            return _cur_message;
        }
        
        /** Set the binary data content to a std::string */
        std::string& message(const std::string &s) {
            m_isChanged = true;
            return _cur_message.assign(s);
        }
        
        /** Set the binary data content to n characters starting from a pointer. */
        std::string& message(const char* s, std::size_t n) {
            m_isChanged = true;
            return _cur_message.assign(s, n);
        }
        
        /** Send data synchronously */
        virtual void sendSync() override {
            // Prepare the Yarp message to send
            _port_content_type & output = this->prepare();
            output.setBinaryData(_cur_message);
            
            // Actually send the message
            this->writeStrict();
            m_isChanged = false;
        }        
        
    protected:
        virtual yarp::os::Contactable& getYarpPort() override {
            return *this;
        }
        
        virtual const yarp::os::Contactable& getYarpPort() const override {
            return *this;
        }
    };
    
}


#endif /* OBNNODE_YARPPORT_H_ */