/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class org_winplus_serial_utils_SerialPort */

#ifndef _Included_org_winplus_serial_utils_SerialPort
#define _Included_org_winplus_serial_utils_SerialPort
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     org_winplus_serial_utils_SerialPort
 * Method:    open
 * Signature: (Ljava/lang/String;II)Ljava/io/FileDescriptor;
 */
JNIEXPORT jobject JNICALL Java_com_yjzn_SerialPort_open
  (JNIEnv *, jclass, jstring, jint, jint);

/*
 * Class:     Java_android_serialport_SerialPort_set_config
 * Method:    close
 * Signature: ()V
 */
JNIEXPORT int JNICALL Java_com_yjzn_SerialPort_set_config
        (JNIEnv *, jobject, jint nBits,jchar nEvent,jint nSpeed,jint nStop);

/*
 * Class:     org_winplus_serial_utils_SerialPort
 * Method:    close
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_yjzn_SerialPort_close
  (JNIEnv *, jobject);

/*
 * Class:     org_winplus_serial_utils_SerialPort
 * Method:    start_scan
 * Signature: ()V
 */

JNIEXPORT void JNICALL Java_com_yjzn_SerialPort_start_1scan
  (JNIEnv *, jobject);
  /*
   * Class:     org_winplus_serial_utils_SerialPort
   * Method:    stop_scan
   * Signature: ()V
   */
  JNIEXPORT void JNICALL Java_com_yjzn_SerialPort_stop_1scan
    (JNIEnv *, jobject);
#ifdef __cplusplus
}
#endif
#endif
