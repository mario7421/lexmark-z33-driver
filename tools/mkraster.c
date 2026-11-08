#include <cups/cups.h>
#include <cups/raster.h>
#include <string.h>
#include <stdlib.h>
int main(void){
    cups_raster_t *r = cupsRasterOpen(1 /*stdout*/, CUPS_RASTER_WRITE);
    cups_page_header2_t h; memset(&h, 0, sizeof(h));
    strcpy(h.MediaClass, "TestPage");
    h.HWResolution[0]=600; h.HWResolution[1]=600;
    h.PageSize[0]=595; h.PageSize[1]=842; h.ImagingBoundingBox[2]=595; h.ImagingBoundingBox[3]=842;
    h.cupsWidth=4958; h.cupsHeight=7017;
    h.cupsBitsPerColor=1; h.cupsBitsPerPixel=1; h.cupsBytesPerLine=620;
    h.cupsColorOrder=CUPS_ORDER_CHUNKED; h.cupsColorSpace=CUPS_CSPACE_K; h.cupsCompression=0;
    if (!cupsRasterWriteHeader2(r, &h)) return 2;
    unsigned char *line = calloc(620, 1);
    for (int y=0; y<7017; y++){
        memset(line, 0, 620);
        if (y > 600 && y < 700) memset(line, 0xff, 620);              /* black bar */
        if (y > 900 && y < 2400 && (y % 20) < 10) line[10]=0xff;      /* vertical dashes */
        if (y > 3000 && y < 3100) memset(line, 0xaa, 620);            /* 50% pattern */
        if (!cupsRasterWritePixels(r, line, 620)) return 3;
    }
    cupsRasterClose(r);
    return 0;
}
