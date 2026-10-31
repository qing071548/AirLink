// Does the rendered QR decode on its own, with no AirLink pipeline involved?
import com.google.zxing.*;
import com.google.zxing.common.HybridBinarizer;
import com.google.zxing.common.GlobalHistogramBinarizer;
import com.google.zxing.qrcode.QRCodeReader;
import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.File;
import java.util.*;

public class QrDirectTest {
    public static void main(String[] a) throws Exception {
        BufferedImage img = ImageIO.read(new File(a[0]));
        int w = img.getWidth(), h = img.getHeight();
        int[] px = new int[w * h];
        img.getRGB(0, 0, w, h, px, 0, w);
        System.out.println("image " + w + "x" + h);

        QRCodeReader reader = new QRCodeReader();
        Map<DecodeHintType, Object> hints = new EnumMap<>(DecodeHintType.class);
        hints.put(DecodeHintType.TRY_HARDER, Boolean.TRUE);

        RGBLuminanceSource src = new RGBLuminanceSource(w, h, px);
        for (String name : new String[] { "hybrid", "global" }) {
            try {
                BinaryBitmap bmp = new BinaryBitmap(
                        name.equals("hybrid") ? new HybridBinarizer(src)
                                               : new GlobalHistogramBinarizer(src));
                Result r = reader.decode(bmp, hints);
                System.out.println(name + " -> " + r.getText());
                return;
            } catch (Exception e) {
                System.out.println(name + " failed: " + e.getClass().getSimpleName());
            }
        }
    }
}
