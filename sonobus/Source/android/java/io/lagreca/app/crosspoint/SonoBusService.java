// SPDX-License-Identifier: GPLv3-or-later
// Copyright (C) 2020 Jesse Chappell

package io.lagreca.app.crosspoint;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Notification;
import android.app.Service;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.IBinder;
import android.os.Binder;
import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;

import android.util.Log;

public class SonoBusService extends Service {

     private final static String TAG = "SonoBus";
    private final static String FOREGROUND_CHANNEL_ID = "foreground_audio_sonobus";
    private NotificationManager mNotificationManager;
    
    public static final int NOTIFICATION_ID_FOREGROUND_SERVICE = 8468343;

    private IBinder mBinder = new MyBinder();
    
    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return mBinder;
    }
    @Override
    public void onCreate() {
        super.onCreate();

        if (BuildConfig.DEBUG) Log.d(TAG, "SonoBus service created");            

        mNotificationManager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        
        //doStartForeground();
    }
    
    public void makeForegroundActive(boolean flag) 
    {
        if (flag) {
            doStartForeground();
        }
        else {
            stopForeground(true);
        }
    }
    
    public void doStartForeground()
    {
        //Toast.makeText(this,"Creating Notification",Toast.LENGTH_SHORT).show();
        
        // handle build version above android oreo
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O &&
            mNotificationManager.getNotificationChannel(FOREGROUND_CHANNEL_ID) == null) {
            CharSequence name = "SonoBus Audio"; //getString(R.string.text_name_notification);
            int importance = NotificationManager.IMPORTANCE_LOW;
            NotificationChannel channel = new NotificationChannel(FOREGROUND_CHANNEL_ID, name, importance);
            channel.enableVibration(false);
            mNotificationManager.createNotificationChannel(channel);
            //Log.d(TAG, "starting notification on O");            
        }
        
        Intent notificationIntent = new Intent(this, SonoBusActivity.class);
        notificationIntent.setAction(Intent.ACTION_MAIN);
        notificationIntent.addCategory(Intent.CATEGORY_LAUNCHER);
        notificationIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        
        // if min sdk goes below honeycomb
        /*if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.HONEYCOMB) {
         notificationIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
         } else {
         notificationIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
         }*/
		 
        int immutableflag = 0x04000000; // done to avoid version checking
        
		PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, notificationIntent, PendingIntent.FLAG_UPDATE_CURRENT | immutableflag);
        
        
        NotificationCompat.Builder notificationBuilder;
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
            notificationBuilder = new NotificationCompat.Builder(this, FOREGROUND_CHANNEL_ID);
        } else {
            notificationBuilder = new NotificationCompat.Builder(this);
        }
        
        notificationBuilder.setContentTitle("SonoBus")
        .setContentText("App is active")
        .setOnlyAlertOnce(true)
        .setAutoCancel(true)
        .setCategory(NotificationCompat.CATEGORY_SERVICE)
        .setPriority(NotificationCompat.PRIORITY_LOW)
        .setContentIntent(pendingIntent)
        .setSmallIcon(R.drawable.ic_stat_crosspoint) // A4: monochrome status-bar icon (design/icon/crosspoint-notification.svg)
        //.setPriority(Notification.PRIORITY_MAX)
        .setOngoing(true).build();
        
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.LOLLIPOP) {
            notificationBuilder.setVisibility(NotificationCompat.VISIBILITY_PUBLIC);
        }
        
        startForeground(1555, notificationBuilder.build());
        
    }
    
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {

        //doStartForeground();
        
        return START_STICKY;
    }
    @Override
    public void onDestroy() {
       if (BuildConfig.DEBUG) Log.d(TAG, "SonoBus service stopped");            

    }
    
    public class MyBinder extends Binder {
        SonoBusService getService() {
            return SonoBusService.this;
        }
    }
    
}
