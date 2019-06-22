package com.camera.usbcamera;

import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.app.Activity;

import java.nio.ByteBuffer;

class CameraPreview extends SurfaceView implements SurfaceHolder.Callback, Runnable {

	private static final boolean DEBUG = true;
	private static final String TAG="WebCam";
	protected Context context;
	private SurfaceHolder holder;
    Thread mainLoop = null;

	private boolean cameraExists=false;
	private boolean shouldStop=false;
	
	// /dev/videox (x=cameraId+cameraBase) is used.
	// In some omap devices, system uses /dev/video[0-3],
	// so users must use /dev/video[4-].
	// In such a case, try cameraId=0 and cameraBase=4
	private int cameraId=0;
	private int cameraBase=0;
	
	// This definition also exists in ImageProc.h.
	// Webcam must support the resolution 640x480 with YUYV format. 
	static final int IMG_WIDTH=1920;
	static final int IMG_HEIGHT=1080;

	// The following variables are used to draw camera images.
    private int winWidth=0;
    private int winHeight=0;
    private Rect rect;
	private Rect rect2;
	private Rect rect_crop;
    private int dw, dh;
    private float rate;

    private ByteBuffer mImageBuffer;
    private int mImageBufferSize;

    private Main mMainActivity;
    
    private RecordingThread mRecordingThread;
    private boolean mRecordingStart;

    void setMainActivity(Activity activity) {
        this.mMainActivity = (Main)activity;
    }
    
    public void startRecording(boolean start) {

        if (null != mRecordingThread) {
            if (start) {
                mRecordingThread.startRecording();
                mRecordingStart = true;
            } else {
        	    mRecordingStart = false;
        		mRecordingThread.stopRecording();
            }
        } else {
            Log.e(TAG, "Recording thread isn't created");
        }
    }
    
	public CameraPreview(Context context) {
		super(context);
		this.context = context;
		if(DEBUG) Log.d(TAG,"CameraPreview constructed");
		setFocusable(true);
		
		holder = getHolder();
		holder.addCallback(this);
		//holder.setType(SurfaceHolder.SURFACE_TYPE_NORMAL);	
	}

	public CameraPreview(Context context, AttributeSet attrs) {
		super(context, attrs);
		this.context = context;
		if(DEBUG) Log.d(TAG,"CameraPreview constructed");
		setFocusable(true);
		
		holder = getHolder();
		holder.addCallback(this);

		mRecordingThread = new RecordingThread();
        mRecordingThread.start();
		//holder.setType(SurfaceHolder.SURFACE_TYPE_NORMAL);	
	}

    private void showErrorDialog(String message) {
        if (mMainActivity != null) {
            mMainActivity.showErrorDialog(message);
        } else {
            Log.w(TAG, "mMainActivity isn't set");
        }
    }
	
    @Override
    public void run() {
        Bitmap bmp;
        int imageSize;
        while (true && cameraExists) {
        	//obtaining display area to draw a large image
        	if(winWidth==0){
        		winWidth=this.getWidth();
        		winHeight=this.getHeight();

        		if(winWidth*IMG_HEIGHT/IMG_WIDTH<=winHeight){
        			dw = 0;
        			dh = (winHeight-winWidth*IMG_HEIGHT/IMG_WIDTH)/2;
        			rate = ((float)winWidth)/IMG_WIDTH;
        			rect = new Rect(dw,dh,dw+winWidth-1,dh+winWidth*IMG_HEIGHT/IMG_WIDTH-1);
        		}else{
        			dw = (winWidth-winHeight*IMG_WIDTH/IMG_HEIGHT)/2;
        			dh = 0;
        			rate = ((float)winHeight)/IMG_HEIGHT;
        			rect = new Rect(dw,dh,dw+winHeight*IMG_WIDTH/IMG_HEIGHT -1,dh+winHeight-1);
        		}

				if(IMG_WIDTH*winWidth/winHeight<=IMG_HEIGHT){
					dw = 0;
					dh = (IMG_HEIGHT-IMG_WIDTH*winWidth/winHeight)/2;
					rect_crop = new Rect(dw,dh,dw+IMG_WIDTH-1,dh+IMG_WIDTH*winHeight/winWidth-1);
				}else{
					dw = (IMG_WIDTH-IMG_HEIGHT*winWidth/winHeight)/2;
					dh = 0;
					rect_crop = new Rect(dw,dh,dw+IMG_HEIGHT*winWidth/winHeight -1,dh+IMG_HEIGHT-1);
				}
				rect2 = new Rect(0,0,winWidth,winHeight);
        	}
        	
        	// obtaining a camera image (pixel data are stored in mImageBuffer).
        	imageSize = UVCJni.readFrame();

    		// Inform recording thread to record frame
        	if (mRecordingStart) {
        		if (null != mRecordingThread) {
        			mRecordingThread.saveFrame();
        		}
        	}

            if (imageSize != 0) {
                if (imageSize != 0) {
                    Canvas canvas = getHolder().lockCanvas();
                    if (canvas != null)
                    {
                        Log.d(TAG, "mImageBuffer: " + mImageBuffer + ", size: " + imageSize + ", length: " + mImageBuffer.array().length);
                        Log.d(TAG, "The first 20 bytes: ");
                        for (int i = 0; i < 20; i++) {
                            Log.d(TAG, Integer.toHexString(mImageBuffer.get(i)&0xff));
                        }

                        Log.d(TAG, "The last 20 bytes: ");
                        for (int i = imageSize - 20; i < imageSize; i++) {
                            Log.d(TAG, Integer.toHexString(mImageBuffer.get(i)&0xff));
                        }
                        
                        byte[] buffer = mImageBuffer.array();
                        Log.d(TAG, "mImageBuffer.array(): offset=" + mImageBuffer.arrayOffset());
                        for (int i = 0; i < 20; i++) {
                            Log.d(TAG, Integer.toHexString(buffer[mImageBuffer.arrayOffset()+i]));
                        }
                        
                        bmp = BitmapFactory.decodeByteArray(mImageBuffer.array(), mImageBuffer.arrayOffset(), imageSize);
                        if (bmp != null) {
                        	// draw camera bmp on canvas
                        	//canvas.drawBitmap(bmp,null,rect,null);
							canvas.drawBitmap(bmp,rect_crop,rect2,null);
                        } else {
                            Log.w(TAG, "Failed to decode mImageBuffer");
                        }
                    	getHolder().unlockCanvasAndPost(canvas);
                    }
                }
            } else {
                Log.w(TAG, "Read Frame failed, imageSize == 0");
            }

            if(shouldStop){
            	shouldStop = false;  
            	break;
            }	        
        }
    }

	@Override
	public void surfaceCreated(SurfaceHolder holder) {
		if(DEBUG) Log.d(TAG, "surfaceCreated");
        
		// /dev/videox (x=cameraId) is used
		int ret = UVCJni.prepareCamera(cameraId);
        if (0 != ret) {
            // Show error dialog
            Log.e(TAG, "Failed to prepareCamera, Please read the log");
            showErrorDialog(context.getString(R.string.prepare_camera));
            return;
        }

        // Set flag to indicate the camera is already initialized
        cameraExists = true;

        // Get the buffer size from JNI, this buffer is using for storing each image data
        mImageBufferSize = UVCJni.getBufferSize();
        if (mImageBufferSize <= 0) {
            Log.e(TAG, "Failed to get buffer size(" + mImageBufferSize + "), Please read the log");
            showErrorDialog(context.getString(R.string.get_buffer_size));
            return;
        }
        
        // Allocate buffer to mImageBuffer
        try {
            mImageBuffer = ByteBuffer.allocateDirect(mImageBufferSize);

            if (mImageBuffer != null) {
                
                Log.d(TAG, "mImageBuffer: " + mImageBuffer + ", length: " + mImageBuffer.array().length);

                // Set the direct buffer, JNI can manipulate this buffer directly
                ret = UVCJni.setDirectBuffer(mImageBuffer, mImageBufferSize);
        		
        		if(ret == 0) {
                    cameraExists = true;
            		
                    mainLoop = new Thread(this);
                    mainLoop.start();
                } else {
                    // Show error dialog
                    Log.e(TAG, "Failed to set direct buffer, Please read the log");
                    showErrorDialog(context.getString(R.string.set_direct_buffer));
                    return;
                }
            } else {
                // Show error dialog
                Log.e(TAG, "Failed to allocate direct buffer, Please read the log");
                showErrorDialog(context.getString(R.string.allocate_direct_buffer_null));
            }
        } catch (IllegalArgumentException e) {
            // Show error dialog
            Log.e(TAG, "Throw Exception for allocating direct buffer, Please read the log");
            showErrorDialog(context.getString(R.string.allocate_direct_buffer_exception));
            
            e.printStackTrace();
        }	
	}
	
	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
		if(DEBUG) Log.d(TAG, "surfaceChanged");
	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		if(DEBUG) Log.d(TAG, "surfaceDestroyed");
        
		if(cameraExists){
			shouldStop = true;
			while(shouldStop){
				try{ 
					Thread.sleep(100); // wait for thread stopping
				}catch(Exception e){}
			}
		    UVCJni.stopCamera();
            cameraExists = false;
		}
	}   
}
