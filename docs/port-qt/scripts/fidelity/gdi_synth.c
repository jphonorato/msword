#include <windows.h>
#include <stdio.h>
int main(void){
    HDC hdc=CreateDCA("DISPLAY",NULL,NULL,NULL);
    struct { const char *f; int b, i; } cases[] = {
        {"Liberation Serif",0,0},{"Liberation Serif",1,0},
        {"Liberation Serif",0,1},{"Liberation Serif",1,1},
        {"Script",0,1},{"Modern",1,0},{"Helv",0,1},{"Tms Rmn",0,1},
    };
    for(unsigned k=0;k<sizeof cases/sizeof*cases;k++){
        LOGFONTA lf; ZeroMemory(&lf,sizeof lf);
        lf.lfHeight=-MulDiv(14,GetDeviceCaps(hdc,LOGPIXELSY),72);
        lf.lfWeight=cases[k].b?FW_BOLD:FW_NORMAL;
        lf.lfItalic=(BYTE)cases[k].i;
        lf.lfCharSet=ANSI_CHARSET;
        lstrcpynA(lf.lfFaceName,cases[k].f,LF_FACESIZE);
        HFONT hf=CreateFontIndirectA(&lf), o=SelectObject(hdc,hf);
        TEXTMETRICA tm; GetTextMetricsA(hdc,&tm);
        char act[LF_FACESIZE]; GetTextFaceA(hdc,LF_FACESIZE,act);
        printf("  %-17s b=%d i=%d -> actual=%-20s overhang=%ld italic=%d weight=%ld\n",
               cases[k].f,cases[k].b,cases[k].i,act,tm.tmOverhang,tm.tmItalic,tm.tmWeight);
        SelectObject(hdc,o); DeleteObject(hf);
    }
    DeleteDC(hdc); return 0;
}
