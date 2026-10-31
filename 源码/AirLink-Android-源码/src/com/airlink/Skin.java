package com.airlink;

import android.graphics.Color;
import android.graphics.Typeface;

/** Light, roomy palette: white cards on a soft grey canvas. */
public final class Skin {
    public static final int BG        = Color.rgb(0xF2, 0xF5, 0xFA);
    public static final int CARD      = Color.WHITE;
    public static final int CARD2     = Color.rgb(0xF7, 0xF9, 0xFC);
    public static final int LINE      = Color.rgb(0xE4, 0xEA, 0xF3);
    public static final int LINE_SOFT = Color.rgb(0xEE, 0xF2, 0xF8);

    public static final int TEXT      = Color.rgb(0x1B, 0x22, 0x30);
    public static final int TEXT2     = Color.rgb(0x5C, 0x67, 0x79);
    public static final int TEXT3     = Color.rgb(0x98, 0xA2, 0xB5);

    public static final int ACCENT    = Color.rgb(0x2F, 0x6B, 0xFF);
    public static final int ACCENT2   = Color.rgb(0x7C, 0x5C, 0xFF);
    public static final int ACCENT_SOFT = Color.rgb(0xE9, 0xF0, 0xFF);

    public static final int GREEN     = Color.rgb(0x16, 0xA3, 0x4A);
    public static final int GREEN_SOFT = Color.rgb(0xE7, 0xF7, 0xEE);
    public static final int ORANGE    = Color.rgb(0xE0, 0x8A, 0x00);
    public static final int ORANGE_SOFT = Color.rgb(0xFD, 0xF3, 0xE0);
    public static final int RED       = Color.rgb(0xE5, 0x48, 0x4D);
    public static final int RED_SOFT  = Color.rgb(0xFD, 0xEC, 0xEC);
    public static final int DIR       = Color.rgb(0xF0, 0xA5, 0x2A);
    public static final int DIR_SOFT  = Color.rgb(0xFD, 0xF3, 0xE2);

    public static final Typeface REG  = Typeface.create("sans-serif", Typeface.NORMAL);
    public static final Typeface MED  = Typeface.create("sans-serif-medium", Typeface.NORMAL);
    public static final Typeface BOLD = Typeface.create("sans-serif", Typeface.BOLD);
    public static final Typeface MONO = Typeface.create("monospace", Typeface.NORMAL);

    public static int alpha(int color, float a) {
        int al = Math.max(0, Math.min(255, (int) (a * 255)));
        return (al << 24) | (color & 0x00FFFFFF);
    }

    public static int mix(int a, int b, float t) {
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        int ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
        int br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
        return Color.rgb((int) (ar + (br - ar) * t), (int) (ag + (bg - ag) * t), (int) (ab + (bb - ab) * t));
    }

    private Skin() {}
}
