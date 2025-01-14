package com.mediatek.hdmi;

import android.os.ServiceManager;
import android.os.SystemProperties;
import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

public class HdimReflectionHelper {

    private static final String TAG = "HdimReflectionHelper";

    private static final String HDMI_MANAGER_CLASS = "com.mediatek.hdmi.IMtkHdmiManager";
    private static final String HDMI_DEF_CLASS = "com.mediatek.hdmi.HdmiDef";

    private static final String HDMI_BOX_MANAGER_CLASS = "com.mediatek.hdmi.HdmiNative";
    private static final String PROPERTY_BOX_SUPPORT = "ro.mtk_alps_box_support";
    public static final boolean HDMI_BOX_SUPPORT = SystemProperties.getBoolean(PROPERTY_BOX_SUPPORT, false);

    public static String getHdmiManagerClass(){
    Log.d(TAG, "getHdmiManagerClass, HDMI_BOX_SUPPORT = " + HDMI_BOX_SUPPORT);
        if (HDMI_BOX_SUPPORT){
            return HDMI_BOX_MANAGER_CLASS;
        }
        return HDMI_MANAGER_CLASS;
    }

    public static int getHdmiDisplayType(Object instance) {
        try {
            Class<?> hdmiManagerClass = Class.forName(getHdmiManagerClass(), false, ClassLoader
                    .getSystemClassLoader());
            return getDeclaredMethod(hdmiManagerClass, instance, "getDisplayType");
        } catch (ClassNotFoundException e) {
            e.printStackTrace();
        }
        return -1;
    }

    public static int getHdmiDisplayTypeConstant(String typeName) {
        try {
            Class<?> hdmiDefClass = Class.forName(HDMI_DEF_CLASS, false, ClassLoader
                    .getSystemClassLoader());
            return (Integer) getNonPublicField(hdmiDefClass, typeName).get(hdmiDefClass);
        } catch (ClassNotFoundException e) {
            e.printStackTrace();
        } catch (IllegalArgumentException e) {
            e.printStackTrace();
        } catch (IllegalAccessException e) {
            e.printStackTrace();
        }
        return -1;
    }

    public static boolean hasCapability(Object instance) {
        try {
            Class<?> hdmiManagerClass = Class.forName(getHdmiManagerClass(), false, ClassLoader
                    .getSystemClassLoader());
            Class<?> paraClass[] = { int.class };
            Method hasCapability = hdmiManagerClass.getDeclaredMethod("hasCapability", paraClass);
            hasCapability.setAccessible(true);
            Object value = hasCapability.invoke(instance,
                    getHdmiDisplayTypeConstant("CAPABILITY_SCALE_ADJUST"));
            return Boolean.valueOf(value.toString());
        } catch (Exception e) {
            e.printStackTrace();
        }
        return false;
    }

    public static int[] getSupportedResolutions(Object instance) {
        try {
            Class<?> hdmiManagerClass = Class.forName(getHdmiManagerClass(), false, ClassLoader
                    .getSystemClassLoader());
            Class<?> paraClass[] = {};
            Method getSupportedResolutions = hdmiManagerClass.getDeclaredMethod(
                    "getSupportedResolutions", paraClass);
            getSupportedResolutions.setAccessible(true);
            Object noObject[] = {};
            Object value = getSupportedResolutions.invoke(instance, noObject);
            return (int[]) value;
        } catch (Exception e) {
            e.printStackTrace();
        }
        return null;
    }

    public static int getAudioParameter(Object instance) {
        try {
            Class<?> hdmiManagerClass = Class.forName(getHdmiManagerClass(), false, ClassLoader
                    .getSystemClassLoader());
            Class<?> paraClass[] = { int.class, int.class };
            Method getAudioParameter = hdmiManagerClass.getDeclaredMethod("getAudioParameter",
                    paraClass);
            getAudioParameter.setAccessible(true);
            Object value = getAudioParameter.invoke(instance,
                    getHdmiDisplayTypeConstant("HDMI_MAX_CHANNEL"),
                    getHdmiDisplayTypeConstant("HDMI_MAX_CHANNEL_OFFSETS"));
            return Integer.valueOf(value.toString());
        } catch (Exception e) {
            e.printStackTrace();
        }
        return -1;
    }

    public static boolean isSignalOutputting(Object instance) {
        try {
            Class<?> hdmiManagerClass = Class.forName(getHdmiManagerClass(), false, ClassLoader
                    .getSystemClassLoader());
            Class<?> paraClass[] = {};
            Method isSignalOutputting = hdmiManagerClass.getDeclaredMethod("isSignalOutputting",
                    paraClass);
            isSignalOutputting.setAccessible(true);
            Object noObject[] = {};
            Object value = isSignalOutputting.invoke(instance, noObject);
            return Boolean.valueOf(value.toString());
        } catch (Exception e) {
            e.printStackTrace();
        }
        return false;
    }

    public static void setVideoResolution(Object instance, int newValue) {
        try {
            Class<?> hdmiManagerClass = Class.forName(getHdmiManagerClass(), false, ClassLoader
                    .getSystemClassLoader());
            Class<?> paraClass[] = { int.class };
            Method setVideoResolution = hdmiManagerClass.getDeclaredMethod("setVideoResolution",
                    paraClass);
            setVideoResolution.setAccessible(true);
            setVideoResolution.invoke(instance, newValue);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public static void setVideoScale(Object instance, int newValue) {
        try {
            Class<?> hdmiManagerClass = Class.forName(getHdmiManagerClass(), false, ClassLoader
                    .getSystemClassLoader());
            Class<?> paraClass[] = { int.class };
            Method setVideoScale = hdmiManagerClass.getDeclaredMethod("setVideoScale", paraClass);
            setVideoScale.setAccessible(true);
            setVideoScale.invoke(instance, newValue);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public static void enableHdmi(Object instance, boolean check) {
        try {
            Class<?> hdmiManagerClass = Class.forName(getHdmiManagerClass(), false, ClassLoader
                    .getSystemClassLoader());
            Class<?> paraClass[] = { boolean.class };
            Method enableHdmi = hdmiManagerClass.getDeclaredMethod("enableHdmi", paraClass);
            enableHdmi.setAccessible(true);
            enableHdmi.invoke(instance, check);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public static Object getHdmiService() {
        Object obj = null;
        Log.d(TAG, "getHdmiService, HDMI_BOX_SUPPORT = " + HDMI_BOX_SUPPORT);
        if (HDMI_BOX_SUPPORT){
            try {
                Class<?> hdmiManagerClass = Class.forName(HDMI_BOX_MANAGER_CLASS, false, ClassLoader
                    .getSystemClassLoader());
                Log.d(TAG, "getHdmiService, hdmiManagerClass = " + hdmiManagerClass);
                Class<?> paraClass[] = {};
                Method method = hdmiManagerClass.getDeclaredMethod("getInstance", paraClass);
                method.setAccessible(true);
                Object noObject[] = {};
                obj = method.invoke(hdmiManagerClass, noObject);
                Log.d(TAG, "getHdmiService, obj = " + obj);
            } catch (Exception e) {
                Log.d(TAG, "getHdmiService, e = " + e);
                obj = null;
            }
        } else {
            obj = (Object) ServiceManager.getService("mtkhdmi");
        }
        Log.d(TAG, "getHdmiService, obj = " + obj);
        return obj;
    }

    public static Field getNonPublicField(Class<?> cls, String fieldName) {
        Field field = null;
        try {
            field = cls.getDeclaredField(fieldName);
            field.setAccessible(true);
        } catch (NoSuchFieldException e) {
            e.printStackTrace();
        }
        return field;
    }

    public static int getDeclaredMethod(Class<?> cls, Object instance, String methodName) {
        try {
            Class<?> paraClass[] = {};
            Method method = cls.getDeclaredMethod(methodName, paraClass);
            method.setAccessible(true);
            Object noObject[] = {};
            Object value = method.invoke(instance, noObject);
            return Integer.valueOf(value.toString());
        } catch (IllegalAccessException e) {
            e.printStackTrace();
        } catch (NoSuchMethodException e) {
            e.printStackTrace();
        } catch (InvocationTargetException e) {
            e.printStackTrace();
        }
        return -1;
    }
}