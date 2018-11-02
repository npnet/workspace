// This source code is generated by UdpGeneratorTool, not recommend to modify it directly
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "Mnld2DebugInterface.h"
#include "mtk_socket_data_coder.h"

// Mnld2DebugInterface_MnldGpsStatusCategory
const char* Mnld2DebugInterface_MnldGpsStatusCategory_to_string(Mnld2DebugInterface_MnldGpsStatusCategory data) {
    switch(data) {
    case MNLD2DEBUG_INTERFACE_MNLD_GPS_STATUS_CATEGORY_GPS_STOPPED:
        return "gpsStopped";
    case MNLD2DEBUG_INTERFACE_MNLD_GPS_STATUS_CATEGORY_GPS_STARTED:
        return "gpsStarted";
    case MNLD2DEBUG_INTERFACE_MNLD_GPS_STATUS_CATEGORY_END:
        return "end";
    default:
        break;
    }
    return "UNKNOWN";
}
void Mnld2DebugInterface_MnldGpsStatusCategory_array_dump(Mnld2DebugInterface_MnldGpsStatusCategory data[], int size) {
    int i = 0;
    SOCK_LOGD("Mnld2DebugInterface_MnldGpsStatusCategory_array_dump() size=[%d]", size);
    for(i = 0; i < size; i++) {
        SOCK_LOGD("  i=[%d] data=[%s]", i, Mnld2DebugInterface_MnldGpsStatusCategory_to_string(data[i]));
    }
}

void Mnld2DebugInterface_MnldGpsStatusCategory_array_init(Mnld2DebugInterface_MnldGpsStatusCategory output[], int max_size) {
    int i = 0;
    for(i = 0; i < max_size; i++) {
        output[i] = MNLD2DEBUG_INTERFACE_MNLD_GPS_STATUS_CATEGORY_GPS_STOPPED;
    }
}

bool Mnld2DebugInterface_MnldGpsStatusCategory_is_equal(Mnld2DebugInterface_MnldGpsStatusCategory data1, Mnld2DebugInterface_MnldGpsStatusCategory data2) {
    return (data1 == data2)? true : false;
}
bool Mnld2DebugInterface_MnldGpsStatusCategory_array_is_equal(Mnld2DebugInterface_MnldGpsStatusCategory data1[], int size1, Mnld2DebugInterface_MnldGpsStatusCategory data2[], int size2) {
    int i = 0;
    if(size1 != size2) return false;
    for(i = 0; i < size1; i++) {
        if(data1[i] != data2[i]) return false;
    }
    return true;
}

bool Mnld2DebugInterface_MnldGpsStatusCategory_encode(char* buff, int* offset, Mnld2DebugInterface_MnldGpsStatusCategory data) {
    switch(data) {
    case MNLD2DEBUG_INTERFACE_MNLD_GPS_STATUS_CATEGORY_GPS_STOPPED:
    case MNLD2DEBUG_INTERFACE_MNLD_GPS_STATUS_CATEGORY_GPS_STARTED:
    case MNLD2DEBUG_INTERFACE_MNLD_GPS_STATUS_CATEGORY_END:
        break;
    default:
        SOCK_LOGE("Mnld2DebugInterface_MnldGpsStatusCategory_encode() unknown data=%d", data);
        return false;
    }
    mtk_socket_put_int(buff, offset, data);
    return true;
}
bool Mnld2DebugInterface_MnldGpsStatusCategory_array_encode(char* buff, int* offset, Mnld2DebugInterface_MnldGpsStatusCategory data[], int size) {
    int i = 0;
    mtk_socket_put_int(buff, offset, size);
    for(i = 0; i < size; i++) {
        if(!Mnld2DebugInterface_MnldGpsStatusCategory_encode(buff, offset, data[i])) {
            SOCK_LOGE("Mnld2DebugInterface_MnldGpsStatusCategory_array_encode() Mnld2DebugInterface_MnldGpsStatusCategory_encode() failed at i=%d", i);
            return false;
        }
    }
    return true;
}

void Mnld2DebugInterface_MnldGpsStatusCategory_decode(char* buff, int* offset, Mnld2DebugInterface_MnldGpsStatusCategory* output) {
    *output = mtk_socket_get_int(buff, offset);
}
int Mnld2DebugInterface_MnldGpsStatusCategory_array_decode(char* buff, int* offset, Mnld2DebugInterface_MnldGpsStatusCategory output[], int max_size) {
    int i = 0;
    int size = mtk_socket_get_int(buff, offset);
    for(i = 0; i < size; i++) {
        if(i < max_size) {
            Mnld2DebugInterface_MnldGpsStatusCategory_decode(buff, offset, &output[i]);
        } else {
            Mnld2DebugInterface_MnldGpsStatusCategory data;
            Mnld2DebugInterface_MnldGpsStatusCategory_decode(buff, offset, &data);
        }
    }
    return (size > max_size)? max_size : size;
}


// Sender
bool Mnld2DebugInterface_mnldAckDebugReq(mtk_socket_fd* client_fd) {
    pthread_mutex_lock(&client_fd->mutex);
    if(!mtk_socket_client_connect(client_fd)) {
        SOCK_LOGE("Mnld2DebugInterface_mnldAckDebugReq() mtk_socket_client_connect() failed");
        pthread_mutex_unlock(&client_fd->mutex);
        return false;
    }
    int _ret;
    char _buff[MNLD2DEBUG_INTERFACE_BUFF_SIZE] = {0};
    int _offset = 0;
    mtk_socket_put_int(_buff, &_offset, MNLD2DEBUG_INTERFACE_PROTOCOL_TYPE);
    mtk_socket_put_int(_buff, &_offset, MNLD2DEBUG_INTERFACE_MNLD_ACK_DEBUG_REQ);
    _ret = mtk_socket_write(client_fd->fd, _buff, _offset);
    if(_ret == -1) {
        SOCK_LOGE("Mnld2DebugInterface_mnldAckDebugReq() mtk_socket_write() failed, fd=%d err=[%s]%d", 
            client_fd, strerror(errno), errno);
        mtk_socket_client_close(client_fd);
        pthread_mutex_unlock(&client_fd->mutex);
        return false;
    }
    mtk_socket_client_close(client_fd);
    pthread_mutex_unlock(&client_fd->mutex);
    return true;
}

bool Mnld2DebugInterface_mnldUpdateReboot(mtk_socket_fd* client_fd) {
    SOCK_LOGE("Mnld2DebugInterface_mnldUpdateReboot");
    pthread_mutex_lock(&client_fd->mutex);
    if(!mtk_socket_client_connect(client_fd)) {
        SOCK_LOGE("Mnld2DebugInterface_mnldUpdateReboot() mtk_socket_client_connect() failed");
        pthread_mutex_unlock(&client_fd->mutex);
        return false;
    }
    int _ret;
    char _buff[MNLD2DEBUG_INTERFACE_BUFF_SIZE] = {0};
    int _offset = 0;
    mtk_socket_put_int(_buff, &_offset, MNLD2DEBUG_INTERFACE_PROTOCOL_TYPE);
    mtk_socket_put_int(_buff, &_offset, MNLD2DEBUG_INTERFACE_MNLD_UPDATE_REBOOT);
    _ret = mtk_socket_write(client_fd->fd, _buff, _offset);
    if(_ret == -1) {
        SOCK_LOGE("Mnld2DebugInterface_mnldUpdateReboot() mtk_socket_write() failed, fd=%d err=[%s]%d", 
            client_fd, strerror(errno), errno);
        mtk_socket_client_close(client_fd);
        pthread_mutex_unlock(&client_fd->mutex);
        return false;
    }
    mtk_socket_client_close(client_fd);
    pthread_mutex_unlock(&client_fd->mutex);
    return true;
}

bool Mnld2DebugInterface_mnldUpdateGpsStatus(mtk_socket_fd* client_fd, Mnld2DebugInterface_MnldGpsStatusCategory status) {
    pthread_mutex_lock(&client_fd->mutex);
    if(!mtk_socket_client_connect(client_fd)) {
        SOCK_LOGE("Mnld2DebugInterface_mnldUpdateGpsStatus() mtk_socket_client_connect() failed");
        pthread_mutex_unlock(&client_fd->mutex);
        return false;
    }
    int _ret;
    char _buff[MNLD2DEBUG_INTERFACE_BUFF_SIZE] = {0};
    int _offset = 0;
    mtk_socket_put_int(_buff, &_offset, MNLD2DEBUG_INTERFACE_PROTOCOL_TYPE);
    mtk_socket_put_int(_buff, &_offset, MNLD2DEBUG_INTERFACE_MNLD_UPDATE_GPS_STATUS);
    if(!Mnld2DebugInterface_MnldGpsStatusCategory_encode(_buff, &_offset, status)) {
        SOCK_LOGE("Mnld2DebugInterface_mnldUpdateGpsStatus() Mnld2DebugInterface_MnldGpsStatusCategory_encode() fail on status");
        mtk_socket_client_close(client_fd);
        pthread_mutex_unlock(&client_fd->mutex);
        return false;
    }
    _ret = mtk_socket_write(client_fd->fd, _buff, _offset);
    if(_ret == -1) {
        SOCK_LOGE("Mnld2DebugInterface_mnldUpdateGpsStatus() mtk_socket_write() failed, fd=%d err=[%s]%d", 
            client_fd, strerror(errno), errno);
        mtk_socket_client_close(client_fd);
        pthread_mutex_unlock(&client_fd->mutex);
        return false;
    }
    mtk_socket_client_close(client_fd);
    pthread_mutex_unlock(&client_fd->mutex);
    return true;
}

bool Mnld2DebugInterface_mnldUpdateMessageInfo(mtk_socket_fd* client_fd, char* msg) {
    pthread_mutex_lock(&client_fd->mutex);
    if(!mtk_socket_client_connect(client_fd)) {
        SOCK_LOGE("Mnld2DebugInterface_mnldUpdateMessageInfo() mtk_socket_client_connect() failed");
        pthread_mutex_unlock(&client_fd->mutex);
        return false;
    }
    int _ret;
    char _buff[MNLD2DEBUG_INTERFACE_BUFF_SIZE] = {0};
    int _offset = 0;
    mtk_socket_put_int(_buff, &_offset, MNLD2DEBUG_INTERFACE_PROTOCOL_TYPE);
    mtk_socket_put_int(_buff, &_offset, MNLD2DEBUG_INTERFACE_MNLD_UPDATE_MESSAGE_INFO);
    if(strlen(msg) > 256) {
        SOCK_LOGE("Mnld2DebugInterface_mnldUpdateMessageInfo() strlen of msg=[%d] is over 256", strlen(msg));
        mtk_socket_client_close(client_fd);
        pthread_mutex_unlock(&client_fd->mutex);
        return false;
    }
    mtk_socket_put_string(_buff, &_offset, msg);
    _ret = mtk_socket_write(client_fd->fd, _buff, _offset);
    if(_ret == -1) {
        SOCK_LOGE("Mnld2DebugInterface_mnldUpdateMessageInfo() mtk_socket_write() failed, fd=%d err=[%s]%d", 
            client_fd, strerror(errno), errno);
        mtk_socket_client_close(client_fd);
        pthread_mutex_unlock(&client_fd->mutex);
        return false;
    }
    mtk_socket_client_close(client_fd);
    pthread_mutex_unlock(&client_fd->mutex);
    return true;
}

// Receiver
bool Mnld2DebugInterface_receiver_decode(char* _buff, Mnld2DebugInterface_callbacks* callbacks) {
    int _ret = 0;
    int _offset = 0;
    _ret = mtk_socket_get_int(_buff, &_offset);
    if(_ret != MNLD2DEBUG_INTERFACE_PROTOCOL_TYPE) {
        SOCK_LOGE("Mnld2DebugInterface_receiver_decode() protocol_type=[%d] is not equals to [%d]",
            _ret, MNLD2DEBUG_INTERFACE_PROTOCOL_TYPE);
        return false;
    }
    _ret = mtk_socket_get_int(_buff, &_offset);
    switch(_ret) {
    case MNLD2DEBUG_INTERFACE_MNLD_ACK_DEBUG_REQ: {
        if(callbacks->Mnld2DebugInterface_mnldAckDebugReq_handler == NULL) {
            SOCK_LOGE("Mnld2DebugInterface_receiver_decode() Mnld2DebugInterface_mnldAckDebugReq_handler() is NULL");
            return false;
        }
        callbacks->Mnld2DebugInterface_mnldAckDebugReq_handler();
        break;
    }
    case MNLD2DEBUG_INTERFACE_MNLD_UPDATE_REBOOT: {
        if(callbacks->Mnld2DebugInterface_mnldUpdateReboot_handler == NULL) {
            SOCK_LOGE("Mnld2DebugInterface_receiver_decode() Mnld2DebugInterface_mnldUpdateReboot_handler() is NULL");
            return false;
        }
        callbacks->Mnld2DebugInterface_mnldUpdateReboot_handler();
        break;
    }
    case MNLD2DEBUG_INTERFACE_MNLD_UPDATE_GPS_STATUS: {
        if(callbacks->Mnld2DebugInterface_mnldUpdateGpsStatus_handler == NULL) {
            SOCK_LOGE("Mnld2DebugInterface_receiver_decode() Mnld2DebugInterface_mnldUpdateGpsStatus_handler() is NULL");
            return false;
        }
        Mnld2DebugInterface_MnldGpsStatusCategory status;
        Mnld2DebugInterface_MnldGpsStatusCategory_decode(_buff, &_offset, &status);
        callbacks->Mnld2DebugInterface_mnldUpdateGpsStatus_handler(status);
        break;
    }
    case MNLD2DEBUG_INTERFACE_MNLD_UPDATE_MESSAGE_INFO: {
        if(callbacks->Mnld2DebugInterface_mnldUpdateMessageInfo_handler == NULL) {
            SOCK_LOGE("Mnld2DebugInterface_receiver_decode() Mnld2DebugInterface_mnldUpdateMessageInfo_handler() is NULL");
            return false;
        }
        char msg[256];
        mtk_socket_get_string(_buff, &_offset, msg, 256);
        callbacks->Mnld2DebugInterface_mnldUpdateMessageInfo_handler(msg);
        break;
    }
    default: {
        SOCK_LOGE("Mnld2DebugInterface_receiver_decode() unknown msgId=[%d]", _ret);
        return false;
    }
    }
    return true;
}
bool Mnld2DebugInterface_receiver_read_and_decode(int server_fd, Mnld2DebugInterface_callbacks* callbacks) {
    int _ret;
    char _buff[MNLD2DEBUG_INTERFACE_BUFF_SIZE] = {0};

    _ret = mtk_socket_read(server_fd, _buff, sizeof(_buff));
    if(_ret == -1) {
        SOCK_LOGE("Mnld2DebugInterface_receiver_read_and_decode() mtk_socket_read() failed, fd=%d err=[%s]%d", 
            server_fd, strerror(errno), errno);
        return false;
    }
    return Mnld2DebugInterface_receiver_decode(_buff, callbacks);
}

