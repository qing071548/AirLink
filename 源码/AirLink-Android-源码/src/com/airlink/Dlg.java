package com.airlink;

import android.app.Dialog;
import android.content.Context;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Light, rounded dialogs that match the app instead of the platform default.
 * The system AlertDialog theme is dark here, which made body text almost
 * unreadable against a dark panel.
 */
public final class Dlg {

    public interface OnText { void run(String text); }

    private static float d(Context c) { return c.getResources().getDisplayMetrics().density; }

    private static GradientDrawable round(Context c, int color, float radiusDp, int strokeColor) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(radiusDp * d(c));
        if (strokeColor != 0) g.setStroke((int) Math.max(1, d(c)), strokeColor);
        return g;
    }

    private static Dialog base(Context ctx) {
        Dialog dlg = new Dialog(ctx);
        dlg.requestWindowFeature(Window.FEATURE_NO_TITLE);
        Window w = dlg.getWindow();
        if (w != null) {
            w.setBackgroundDrawable(new ColorDrawable(0x00000000));
            w.setDimAmount(0.45f);
            w.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        }
        return dlg;
    }

    /** Column card: title, optional body, optional custom content, two buttons. */
    public static Dialog card(Context ctx, String title, String body, View extra,
                              String positive, Runnable onPositive,
                              String negative, Runnable onNegative) {
        final Dialog dlg = base(ctx);
        float dd = d(ctx);

        FrameLayout outer = new FrameLayout(ctx);
        int pad = (int) (22 * dd);

        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(pad, pad, pad, (int) (12 * dd));
        card.setBackground(round(ctx, 0xFFFFFFFF, 18, 0xFFE4EAF3));

        if (title != null && !title.isEmpty()) {
            TextView t = new TextView(ctx);
            t.setText(title);
            t.setTextSize(16.5f);
            t.setTextColor(Skin.TEXT);
            t.setTypeface(Skin.BOLD);
            card.addView(t);
        }
        if (body != null && !body.isEmpty()) {
            TextView b = new TextView(ctx);
            b.setText(body);
            b.setTextSize(13);
            b.setTextColor(Skin.TEXT2);
            b.setLineSpacing(0, 1.25f);
            LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            bl.topMargin = (int) (10 * dd);
            card.addView(b, bl);
        }
        if (extra != null) {
            LinearLayout.LayoutParams el = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            el.topMargin = (int) (14 * dd);
            card.addView(extra, el);
        }

        LinearLayout btns = new LinearLayout(ctx);
        btns.setOrientation(LinearLayout.HORIZONTAL);
        btns.setGravity(Gravity.END);
        LinearLayout.LayoutParams rowLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        rowLp.topMargin = (int) (18 * dd);
        card.addView(btns, rowLp);

        if (negative != null) {
            TextView n = button(ctx, negative, Skin.TEXT2, 0xFFF2F5FA);
            btns.addView(n);
            n.setOnClickListener(v -> { dlg.dismiss(); if (onNegative != null) onNegative.run(); });
            ((LinearLayout.LayoutParams) n.getLayoutParams()).rightMargin = (int) (10 * dd);
        }
        if (positive != null) {
            TextView p = button(ctx, positive, 0xFFFFFFFF, Skin.ACCENT);
            btns.addView(p);
            p.setOnClickListener(v -> { dlg.dismiss(); if (onPositive != null) onPositive.run(); });
        }

        FrameLayout.LayoutParams cl = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        cl.leftMargin = (int) (28 * dd);
        cl.rightMargin = (int) (28 * dd);
        outer.addView(card, cl);
        dlg.setContentView(outer);
        return dlg;
    }

    private static TextView button(Context ctx, String label, int textColor, int bg) {
        float dd = d(ctx);
        TextView v = new TextView(ctx);
        v.setText(label);
        v.setTextSize(13.5f);
        v.setTextColor(textColor);
        v.setTypeface(Skin.MED);
        v.setGravity(Gravity.CENTER);
        v.setPadding((int) (20 * dd), (int) (10 * dd), (int) (20 * dd), (int) (10 * dd));
        v.setBackground(round(ctx, bg, 11, 0));
        v.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return v;
    }

    /** Text prompt with a properly styled field. */
    public static void input(Context ctx, String title, String hint, String initial,
                             String hint2, String positive, OnText onOk) {
        final Dialog dlg = base(ctx);
        float dd = d(ctx);

        FrameLayout outer = new FrameLayout(ctx);
        int pad = (int) (22 * dd);
        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(pad, pad, pad, (int) (12 * dd));
        card.setBackground(round(ctx, 0xFFFFFFFF, 18, 0xFFE4EAF3));

        TextView t = new TextView(ctx);
        t.setText(title);
        t.setTextSize(16.5f);
        t.setTextColor(Skin.TEXT);
        t.setTypeface(Skin.BOLD);
        card.addView(t);

        if (hint2 != null && !hint2.isEmpty()) {
            TextView s = new TextView(ctx);
            s.setText(hint2);
            s.setTextSize(12);
            s.setTextColor(Skin.TEXT3);
            s.setLineSpacing(0, 1.2f);
            LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            sl.topMargin = (int) (8 * dd);
            card.addView(s, sl);
        }

        final EditText et = new EditText(ctx);
        et.setText(initial == null ? "" : initial);
        et.setHint(hint == null ? "" : hint);
        et.setHintTextColor(Skin.TEXT3);
        et.setTextColor(Skin.TEXT);
        et.setTextSize(14.5f);
        et.setSingleLine(true);
        et.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        et.setPadding((int) (14 * dd), (int) (12 * dd), (int) (14 * dd), (int) (12 * dd));
        et.setBackground(round(ctx, Skin.CARD2, 12, Skin.LINE));
        et.setSelectAllOnFocus(true);
        LinearLayout.LayoutParams el = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        el.topMargin = (int) (16 * dd);
        card.addView(et, el);

        LinearLayout btns = new LinearLayout(ctx);
        btns.setOrientation(LinearLayout.HORIZONTAL);
        btns.setGravity(Gravity.END);
        LinearLayout.LayoutParams rowLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        rowLp.topMargin = (int) (16 * dd);
        card.addView(btns, rowLp);

        TextView n = button(ctx, "取消", Skin.TEXT2, 0xFFF2F5FA);
        btns.addView(n);
        ((LinearLayout.LayoutParams) n.getLayoutParams()).rightMargin = (int) (10 * dd);
        n.setOnClickListener(v -> dlg.dismiss());

        TextView p = button(ctx, positive, 0xFFFFFFFF, Skin.ACCENT);
        btns.addView(p);
        p.setOnClickListener(v -> {
            String s = et.getText().toString().trim();
            dlg.dismiss();
            if (onOk != null) onOk.run(s);
        });

        FrameLayout.LayoutParams cl = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        cl.leftMargin = (int) (24 * dd);
        cl.rightMargin = (int) (24 * dd);
        outer.addView(card, cl);
        dlg.setContentView(outer);
        dlg.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE);
        dlg.show();
        et.requestFocus();
        et.setSelection(et.getText().length());
    }

    private Dlg() {}
}
