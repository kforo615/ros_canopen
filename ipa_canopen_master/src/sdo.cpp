#include <ipa_canopen_master/canopen.h>

using namespace ipa_canopen;

const uint8_t COMMAND_MASK =  (1<<7) | (1<<6) | (1<<5);
const uint8_t INITIATE_DOWNLOAD_REQUEST =  (0 << 5);
const uint8_t INITIATE_DOWNLOAD_RESPONSE =  (1 << 5);
const uint8_t DOWNLOAD_SEGMENT_REQUEST =  (1 << 5);
const uint8_t DOWNLOAD_SEGMENT_RESPONSE =  (3 << 5);
const uint8_t INITIATE_UPLOAD_REQUEST =  (2 << 5);
const uint8_t INITIATE_UPLOAD_RESPONSE =  (2 << 5);
const uint8_t UPLOAD_SEGMENT_REQUEST =  (3 << 5);
const uint8_t UPLOAD_SEGMENT_RESPONSE =  (0 << 5);
const uint8_t ABORT_TRANSFER_REQUEST =  (4 << 5);


#pragma pack(push) /* push current alignment to stack */
#pragma pack(1) /* set alignment to 1 byte boundary */

struct SDOid{
    uint32_t id:29;
    uint32_t extended:1;
    uint32_t dynamic:1;
    uint32_t invalid:1;
    SDOid(uint32_t val){
        *(uint32_t*) this = val;
    }
    ipa_can::Header header() {
        return ipa_can::Header(id, extended);
    }
};

struct InitiateShort{
    uint8_t :5;
    uint8_t command:3;
    uint16_t index;
    uint8_t sub_index;
    uint8_t reserved[4];
};

struct InitiateLong{
    uint8_t size_indicated:1;
    uint8_t expedited:1;
    uint8_t num:2;
    uint8_t :1;
    uint8_t command:3;
    uint16_t index;
    uint8_t sub_index;
    uint8_t payload[4];
    
    size_t data_size(){
        if(expedited && size_indicated) return 4-num;
        else if(!expedited && size_indicated) return payload[0] | (payload[3]<<8);
        else return 0;
    }
    size_t apply_buffer(const std::string &buffer){
        size_t size = buffer.size();
        size_indicated = 1;
        if(size > 4){
            expedited = 0;
            payload[0] = size & 0xFF;
            payload[3] = (size >> 8) & 0xFF;
            return 0;
        }else{
            expedited = 1;
            size_indicated = 1;
            num = 4-size;
            memcpy(payload, buffer.data(), size);
            return size;
        }
    }
};

struct SegmentShort{
    uint8_t :4;
    uint8_t toggle:1;
    uint8_t command:3;
    uint8_t reserved[7];
};

struct SegmentLong{
    uint8_t done:1;
    uint8_t num:3;
    uint8_t toggle:1;
    uint8_t command:3;
    uint8_t payload[7];
    size_t data_size(){
        return 7-num;
    }
    size_t apply_buffer(const std::string &buffer, const size_t offset){
        size_t size = buffer.size() - offset;
        if(size > 7) size = 7;
        num = size;
        return offset + size;
    }
};

struct DownloadInitiateRequest: public FrameOverlay<InitiateLong>{
    static const uint8_t command = 1;
    
    DownloadInitiateRequest(const Header &h, const ipa_canopen::ObjectDict::Entry &entry, const std::string &buffer, size_t &offset) : FrameOverlay(h) {
        data.command = command;
        data.index = entry.index;
        data.sub_index = entry.sub_index;
        offset = data.apply_buffer(buffer);
   }
    DownloadInitiateRequest(const ipa_can::Frame &f) : FrameOverlay(f){ }
};

struct DownloadInitiateResponse: public FrameOverlay<InitiateShort>{
    static const uint8_t command = 3;
    
    DownloadInitiateResponse(const ipa_can::Frame &f) : FrameOverlay(f){ }
    
    bool test(const ipa_can::Frame  &msg, uint32_t &reason){
        DownloadInitiateRequest req(msg);
        if(req.data.command ==  DownloadInitiateRequest::command && data.index == req.data.index && data.sub_index == req.data.sub_index){
            return true;
        }
        reason = 0x08000000; // General error
        return false;
    }
};

struct DownloadSegmentRequest: public FrameOverlay<SegmentLong>{
    static const uint8_t command = 0;
    
    DownloadSegmentRequest(const ipa_can::Frame &f) : FrameOverlay(f){ }
    
    DownloadSegmentRequest(const Header &h, bool toggle, const std::string &buffer, size_t& offset) : FrameOverlay(h) {
        data.command = command;
        data.toggle = toggle?1:0;
        offset = data.apply_buffer(buffer, offset);
    }
};

struct DownloadSegmentResponse : public FrameOverlay<SegmentShort>{
    static const uint8_t command = 1;
    DownloadSegmentResponse(const ipa_can::Frame &f) : FrameOverlay(f) {
    }
    bool test(const ipa_can::Frame  &msg, uint32_t &reason){
        DownloadSegmentRequest req(msg);
        if (req.data.command !=  DownloadSegmentRequest::command){
            reason = 0x08000000; // General error
            return false;
        }else if( data.toggle != req.data.toggle){
            reason = 0x05030000; // Toggle bit not alternated
            return false;
        }
        return true;
    }
};

struct UploadInitiateRequest: public FrameOverlay<InitiateShort>{
    static const uint8_t command = 2;
    UploadInitiateRequest(const Header &h, const ipa_canopen::ObjectDict::Entry &entry) : FrameOverlay(h) {
        data.command = command;
        data.index = entry.index;
        data.sub_index = entry.sub_index;
   }
    UploadInitiateRequest(const ipa_can::Frame &f) : FrameOverlay(f){ }
};

struct UploadInitiateResponse: public FrameOverlay<InitiateLong>{
    static const uint8_t command = 2;
    UploadInitiateResponse(const ipa_can::Frame &f) : FrameOverlay(f) { }
    bool test(const ipa_can::Frame  &msg, size_t size, uint32_t &reason){
        UploadInitiateRequest req(msg);
        if(req.data.command ==  UploadInitiateRequest::command && data.index == req.data.index && data.sub_index == req.data.sub_index){
                size_t ds = data.data_size();
                if(ds == 0  || size == 0 || ds == size) {
                    if(!data.expedited || (ds <= 4 && size <= 4)) return true;
                }else{
                    reason = 0x06070010; // Data type does not match, length of service parameter does not match                    
                    return false;
                }
        }
        reason = 0x08000000; // General error
        return false;
    }
    bool read_data(std::string & buffer, size_t & offset, size_t & total){
        if(data.expedited){
            memcpy(&buffer[0], data.payload, buffer.size());
            offset = buffer.size();
            return true;
        }else if(data.size_indicated && total == 0){
            total = data.data_size();
            buffer.resize(total);
        }
        return false;
    }
};
struct UploadSegmentRequest: public FrameOverlay<SegmentShort>{
    static const uint8_t command = 3;
    UploadSegmentRequest(const Header &h, bool toggle) : FrameOverlay(h) {
        data.command = command;
        data.toggle = toggle?1:0;
   }
    UploadSegmentRequest(const ipa_can::Frame &f) : FrameOverlay(f) { }
};

struct UploadSegmentResponse : public FrameOverlay<SegmentLong>{
    static const uint8_t command = 0;
    UploadSegmentResponse(const ipa_can::Frame &f) : FrameOverlay(f) {
    }
    bool test(const ipa_can::Frame  &msg, uint32_t &reason){
        UploadSegmentRequest req(msg);
        if(req.data.command !=  UploadSegmentRequest::command){
            reason = 0x08000000; // General error
            return false;
        }else if( data.toggle != req.data.toggle){
            reason = 0x05030000; // Toggle bit not alternated
            return false;
        }
        return true;
    }
    bool read_data(std::string & buffer, size_t & offset, const size_t & total){
        uint32_t n = data.data_size();
        if(total == 0){
            buffer.resize(offset + n);
        }
        if(offset +  n <= buffer.size()){
            memcpy(&buffer[offset], data.payload, n);
            offset +=  n;
            return true;
        }
        return false;
    }
};

struct AbortData{
    uint8_t :5;
    uint8_t command:3;
    uint16_t index;
    uint8_t sub_index;
    uint32_t reason;
    
    const char * text(){
        switch(reason){
        case 0x05030000: return "Toggle bit not alternated.";
        case 0x05040000: return "SDO protocol timed out.";
        case 0x05040001: return "Client/server command specifier not valid or unknown.";
        case 0x05040002: return "Invalid block size (block mode only).";
        case 0x05040003: return "Invalid sequence number (block mode only).";
        case 0x05040004: return "CRC error (block mode only).";
        case 0x05040005: return "Out of memory.";
        case 0x06010000: return "Unsupported access to an object.";
        case 0x06010001: return "Attempt to read a write only object.";
        case 0x06010002: return "Attempt to write a read only object.";
        case 0x06020000: return "Object does not exist in the object dictionary.";
        case 0x06040041: return "Object cannot be mapped to the PDO.";
        case 0x06040042: return "The number and length of the objects to be mapped would exceed PDO length.";
        case 0x06040043: return "General parameter incompatibility reason.";
        case 0x06040047: return "General internal incompatibility in the device.";
        case 0x06060000: return "Access failed due to an hardware error.";
        case 0x06070010: return "Data type does not match, length of service parameter does not match";
        case 0x06070012: return "Data type does not match, length of service parameter too high";
        case 0x06070013: return "Data type does not match, length of service parameter too low";
        case 0x06090011: return "Sub-index does not exist.";
        case 0x06090030: return "Invalid value for parameter (download only).";
        case 0x06090031: return "Value of parameter written too high (download only).";
        case 0x06090032: return "Value of parameter written too low (download only).";
        case 0x06090036: return "Maximum value is less than minimum value.";
        case 0x060A0023: return "Resource not available: SDO connection";
        case 0x08000000: return "General error";
        case 0x08000020: return "Data cannot be transferred or stored to the application.";
        case 0x08000021: return "Data cannot be transferred or stored to the application because of local control.";
        case 0x08000022: return "Data cannot be transferred or stored to the application because of the present device state.";
        case 0x08000023: return "Object dictionary dynamic generation fails or no object dictionary is present (e.g.object dictionary is generated from file and generation fails because of an file error).";
        case 0x08000024: return "No data available";
        default: return "Abort code is reserved";
        }
    }
};

struct AbortTranserRequest: public FrameOverlay<AbortData>{
    static const uint8_t command = 4;
    AbortTranserRequest(const ipa_can::Frame &f) : FrameOverlay(f) {}
    AbortTranserRequest(const Header &h, uint16_t index, uint8_t sub_index, uint32_t reason) : FrameOverlay(h) {
        data.command = command;
        data.index = index;
        data.sub_index = sub_index;
        data.reason = reason;
   }
};

#pragma pack(pop) /* pop previous alignment from stack */

void SDOClient::abort(uint32_t reason){
    if(current_entry){
        interface_->send(last_msg = AbortTranserRequest(client_id, current_entry->index, current_entry->sub_index, reason));
    }
}

void SDOClient::handleFrame(const ipa_can::Frame & msg){
    boost::mutex::scoped_lock cond_lock(cond_mutex);
    assert(msg.dlc == 8);
    
    bool notify = false;
    uint32_t reason = 0;
    switch(msg.data[0] >> 5){
        case DownloadInitiateResponse::command:
        {
            DownloadInitiateResponse resp(msg);
            if( resp.test(last_msg, reason) ){
                if(offset < total){
                    interface_->send(last_msg = DownloadSegmentRequest(client_id, false, buffer, offset));
                }else{
                    notify = true;
                }
            }
            break;
        }
        case DownloadSegmentResponse::command:
        {
            DownloadSegmentResponse resp(msg);
            if( resp.test(last_msg, reason) ){
                if(offset < total){
                    interface_->send(last_msg = DownloadSegmentRequest(client_id, !resp.data.toggle, buffer, offset));
                }else{
                    notify = true;
                }
            }
            break;
        }
            
        case UploadInitiateResponse::command:
        {
            UploadInitiateResponse resp(msg);
            if( resp.test(last_msg, total, reason) ){
                if(resp.read_data(buffer, offset, total)){
                    notify = true;
                }else{
                    interface_->send(last_msg = UploadSegmentRequest(client_id, false));
                }
            }
            break;
        }
        case UploadSegmentResponse::command:
        {
            UploadSegmentResponse resp(msg);
            if( resp.test(last_msg, reason) ){
                if(resp.read_data(buffer, offset, total)){
                    if(resp.data.done || offset == total){
                    notify = true;
                    }else{
                        interface_->send(last_msg = UploadSegmentRequest(client_id, !resp.data.toggle));
                    }
                }else{
                    // abort, size mismatch
                    LOG("abort, size mismatch" << buffer.size() << " " << resp.data.data_size());
                    reason = 0x06070010; // Data type does not match, length of service parameter does not match
                }
            }
            break;
        }
        case AbortTranserRequest::command:
            LOG("abort, reason: " << AbortTranserRequest(msg).data.text());
            offset = 0;
            notify = true;
            break;
    }
    if(reason){
        abort(reason);
        offset = 0;
        notify = true;
    }
    if(notify){
        cond_lock.unlock();
        cond.notify_one();
    }
        
}    
    
void SDOClient::init(){
    assert(storage_);
    assert(interface_);

    try{
        client_id = SDOid(storage_->entry<uint32_t>(0x1200, 1).get_cached()).header();
    }
    catch(...){
        client_id = ipa_can::Header(0x600+ storage_->node_id_);
    }
    
    last_msg = AbortTranserRequest(client_id, 0,0,0);
    current_entry = 0;

    ipa_can::Header server_id;
    try{
        server_id = SDOid(storage_->entry<uint32_t>(0x1200, 2).get_cached()).header();
    }
    catch(...){
        server_id = ipa_can::Header(0x580+ storage_->node_id_);
    }
    listener_ = interface_->createMsgListener(server_id, ipa_can::Interface::FrameDelegate(this, &SDOClient::handleFrame));
}
void SDOClient::wait_for_response(){
    boost::mutex::scoped_lock cond_lock(cond_mutex);
    if(!cond.timed_wait(cond_lock,boost::posix_time::seconds(1)))
    {
        abort(0x05040000); // SDO protocol timed out.
        throw TimeoutException();
    }
    if(offset == 0 || offset != total){
        throw TimeoutException(); // TODO
    }
}
void SDOClient::read(const ipa_canopen::ObjectDict::Entry &entry, std::string &data){
    boost::timed_mutex::scoped_lock lock(mutex, boost::posix_time::seconds(2));
    if(lock){

        buffer = data;
        offset = 0;
        total = buffer.size();
        current_entry = &entry;

        interface_->send(last_msg = UploadInitiateRequest(client_id, entry));

        wait_for_response();
        data = buffer;
    }else{
        throw TimeoutException();
    }
}
void SDOClient::write(const ipa_canopen::ObjectDict::Entry &entry, const std::string &data){
    boost::timed_mutex::scoped_lock lock(mutex, boost::posix_time::seconds(2));
    if(lock){
        buffer = data;
        offset = 0;
        total = buffer.size();
        current_entry = &entry;

        interface_->send(last_msg = DownloadInitiateRequest(client_id, entry, buffer, offset));

        wait_for_response();
    }else{
        throw TimeoutException();
    }
}
