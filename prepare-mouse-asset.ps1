$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
public static class PulseAsset {
    public static string Prepare(string input, string output) {
        using (var source = new Bitmap(input)) {
            int left=source.Width, top=source.Height, right=-1, bottom=-1;
            int transparent=0;
            for(int y=0;y<source.Height;y++) for(int x=0;x<source.Width;x++) {
                var alpha=source.GetPixel(x,y).A;
                if(alpha==0)transparent++;
                if(alpha>16){left=Math.Min(left,x);top=Math.Min(top,y);right=Math.Max(right,x);bottom=Math.Max(bottom,y);}
            }
            if(transparent==0 || right<left)throw new Exception("Expected a transparent product cutout");
            var crop=new Rectangle(left,top,right-left+1,bottom-top+1);
            int width=256, height=(int)Math.Round(width*crop.Height/(double)crop.Width);
            using(var bitmap=new Bitmap(width,height,PixelFormat.Format32bppArgb))
            using(var g=Graphics.FromImage(bitmap)) {
                g.CompositingMode=CompositingMode.SourceCopy;
                g.InterpolationMode=InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode=PixelOffsetMode.HighQuality;
                using(var attributes=new ImageAttributes()) {
                    attributes.SetWrapMode(WrapMode.TileFlipXY);
                    g.DrawImage(source,new Rectangle(0,0,width,height),crop.X,crop.Y,crop.Width,crop.Height,GraphicsUnit.Pixel,attributes);
                }
                bitmap.Save(output,ImageFormat.Png);
            }
            return String.Format("Transparent pixels: {0}; cropped bounds: {1}; packaged asset: {2}x{3}",transparent,crop,width,height);
        }
    }
}
'@ -ReferencedAssemblies @('System.Drawing.Common','System.Drawing.Primitives','System.Private.Windows.GdiPlus','System.Private.Windows.Core')
[PulseAsset]::Prepare((Join-Path $PSScriptRoot 'assets\basilisk-cutout.png'),(Join-Path $PSScriptRoot 'src\mouse.png'))
