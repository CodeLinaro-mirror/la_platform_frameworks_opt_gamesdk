/*
 * Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.samples.cube;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

public class FrameTimeGraphView extends View {
    private static final int BUFFER_SIZE = 150;
    private final float[] mFrameTimes = new float[BUFFER_SIZE];
    private int mHead = 0;
    private int mCount = 0;
    private float mTargetMs = 16.67f;

    private final Paint mBgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mBarPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mLinePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mTextPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mBgRect = new RectF();

    public FrameTimeGraphView(Context context) {
        super(context);
        init();
    }

    public FrameTimeGraphView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public FrameTimeGraphView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        mBgPaint.setColor(Color.parseColor("#CC18181C")); // Faded dark glassmorphism

        mBarPaint.setStyle(Paint.Style.STROKE);
        mBarPaint.setStrokeCap(Paint.Cap.ROUND);

        mLinePaint.setColor(Color.parseColor("#44FFFFFF"));
        mLinePaint.setStyle(Paint.Style.STROKE);
        mLinePaint.setStrokeWidth(2f);
        mLinePaint.setPathEffect(new DashPathEffect(new float[] {10f, 10f}, 0f));

        mTextPaint.setColor(Color.parseColor("#B3FFFFFF")); // Faint white text
        mTextPaint.setTextSize(24f);
    }

    public void addFrameTime(float timeMs) {
        mFrameTimes[mHead] = timeMs;
        mHead = (mHead + 1) % BUFFER_SIZE;
        if (mCount < BUFFER_SIZE) {
            mCount++;
        }
        invalidate();
    }

    public void setTargetMs(float targetMs) {
        if (targetMs > 0) {
            mTargetMs = targetMs;
            invalidate();
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        int width = getWidth();
        int height = getHeight();
        if (width == 0 || height == 0) {
            return;
        }

        // Draw background panel
        mBgRect.set(0, 0, width, height);
        canvas.drawRoundRect(mBgRect, 16f, 16f, mBgPaint);

        // Find max frame time in current buffer to auto-scale graph if there's a huge spike
        float maxVal = 50f; // Baseline max height (50 ms)
        for (int i = 0; i < mCount; i++) {
            if (mFrameTimes[i] > maxVal) {
                maxVal = mFrameTimes[i];
            }
        }

        // Render guidelines
        drawGuideline(canvas, 16.67f, maxVal, width, height, "16.6 ms (60 FPS)");
        drawGuideline(canvas, 33.33f, maxVal, width, height, "33.3 ms (30 FPS)");
        if (Math.abs(mTargetMs - 16.67f) > 0.1f && Math.abs(mTargetMs - 33.33f) > 0.1f) {
            drawGuideline(canvas, mTargetMs, maxVal, width, height,
                    String.format("%.1f ms (Target)", mTargetMs));
        }

        // Draw vertical bars for frame times
        if (mCount == 0) {
            return;
        }

        float barWidth = (float) width / BUFFER_SIZE;
        mBarPaint.setStrokeWidth(Math.max(2f, barWidth - 2f));

        int startIndex = mCount < BUFFER_SIZE ? 0 : mHead;
        for (int i = 0; i < mCount; i++) {
            int index = (startIndex + i) % BUFFER_SIZE;
            float val = mFrameTimes[index];

            // Map to screen Y (0 ms = height - 16, maxVal = 16)
            float startX = i * barWidth + barWidth / 2f;
            float endY = height - 16f - ((val / maxVal) * (height - 32f));
            float startY = height - 16f;

            // Color scheme: Red for missed frames, Green for hit target frames
            if (val > mTargetMs + 2.0f) {
                mBarPaint.setColor(Color.parseColor("#FFFF4444")); // Janky/Missed frame (Red)
            } else if (val > mTargetMs + 0.5f) {
                mBarPaint.setColor(Color.parseColor("#FFFFBB33")); // Borderline (Orange/Yellow)
            } else {
                mBarPaint.setColor(Color.parseColor("#FF00C851")); // Smooth (Green)
            }

            canvas.drawLine(startX, startY, startX, Math.min(startY - 2f, endY), mBarPaint);
        }
    }

    private void drawGuideline(
            Canvas canvas, float valMs, float maxVal, int width, int height, String label) {
        if (valMs > maxVal) {
            return;
        }
        float y = height - 16f - ((valMs / maxVal) * (height - 32f));
        canvas.drawLine(30f, y, width - 30f, y, mLinePaint);
        canvas.drawText(label, 40f, y - 8f, mTextPaint);
    }
}
