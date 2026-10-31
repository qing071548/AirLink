// Standalone check of AirLink's scan pipeline: build a fake NV21 camera frame
// from the rendered QR, then run exactly the steps ScanActivity runs.
import com.google.zxing.*;
import com.google.zxing.common.HybridBinarizer;
import com.google.zxing.qrcode.QRCodeReader;
import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.File;
import java.util.*;

public class QrPipelineTest {
    static byte[] rotateYuv(byte[] src, int w, int h, int deg) {
        if (deg == 0) return src;
        byte[] out = new byte[src.length];
        if (deg == 180) {
            for (int i = 0; i < w * h; i++) out[i] = src[w * h - 1 - i];
            return out;
        }
        int nw = h;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int dx, dy;
                if (deg == 90) { dx = h - 1 - y; dy = x; }
                else { dx = y; dy = w - 1 - x; }
                out[dy * nw + dx] = src[y * w + x];
            }
        }
        for (int i = w * h; i < out.length; i++) out[i] = (byte) 128;
        return out;
    }

    static Result decodeMagnified(byte[] yuv, int w, int h,
                                  Map<DecodeHintType, Object> hints, QRCodeReader reader) {
        try {
            int cw = w / 2, ch = h / 2;
            int ox = (w - cw) / 2, oy = (h - ch) / 2;
            int zw = cw * 2, zh = ch * 2;
            byte[] zoom = new byte[zw * zh * 3 / 2];
            for (int y = 0; y < zh; y++) {
                int srow = (oy + y / 2) * w + ox;
                int drow = y * zw;
                for (int x = 0; x < zw; x++) zoom[drow + x] = yuv[srow + x / 2];
            }
            for (int i = zw * zh; i < zoom.length; i++) zoom[i] = (byte) 128;
            PlanarYUVLuminanceSource s =
                    new PlanarYUVLuminanceSource(zoom, zw, zh, 0, 0, zw, zh, false);
            return reader.decode(new BinaryBitmap(new HybridBinarizer(s)), hints);
        } catch (Exception e) {
            return null;
        }
    }

    public static void main(String[] a) throws Exception {
        BufferedImage qr = ImageIO.read(new File(a[0]));
        int W = 1280, H = 720, rot = 90;
        // paste the QR into a landscape frame at the requested size
        int qrPx = Integer.parseInt(a[1]);
        byte[] frame = new byte[W * H * 3 / 2];
        Arrays.fill(frame, (byte) 200);           // bright background
        int ox = (W - qrPx) / 2, oy = (H - qrPx) / 2;
        for (int y = 0; y < qrPx; y++) {
            for (int x = 0; x < qrPx; x++) {
                int rgb = qr.getRGB(x * qr.getWidth() / qrPx, y * qr.getHeight() / qrPx);
                int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
                frame[(oy + y) * W + (ox + x)] = (byte) ((r * 299 + g * 587 + b * 114) / 1000);
            }
        }
        byte[] upright = rotateYuv(frame, W, H, rot);
        int dw = (rot == 90 || rot == 270) ? H : W;
        int dh = (rot == 90 || rot == 270) ? W : H;

        Map<DecodeHintType, Object> hints = new EnumMap<>(DecodeHintType.class);
        hints.put(DecodeHintType.TRY_HARDER, Boolean.TRUE);
        QRCodeReader reader = new QRCodeReader();

        PlanarYUVLuminanceSource src =
                new PlanarYUVLuminanceSource(upright, dw, dh, 0, 0, dw, dh, false);
        String text = null;
        try {
            text = reader.decode(new BinaryBitmap(new HybridBinarizer(src)), hints).getText();
        } catch (Exception e) {
            Result r2 = decodeMagnified(upright, dw, dh, hints, reader);
            if (r2 != null) text = r2.getText();
        }
        System.out.println("qrPx=" + qrPx + " upright=" + dw + "x" + dh);
        System.out.println(text == null ? "DECODE FAILED" : "decoded: " + text);
    }
}
