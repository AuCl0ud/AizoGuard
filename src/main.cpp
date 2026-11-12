#ifndef UNICODE
#define UNICODE
#endif
#define _WIN32_WINNT 0x0601
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <mmsystem.h>

#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "audio.h"
// Independently implemented normalized correlation; no MAA source is copied.
struct Image {
    int w=0,h=0;
    std::vector<unsigned char> pixels;
    bool valid() const { return w>0 && h>0 && pixels.size()==size_t(w)*h*4; }
    double gray(int x,int y) const {
        const auto* p=&pixels[(size_t(y)*w+x)*4];
        return p[0]*0.114+p[1]*0.587+p[2]*0.299;
    }
};
struct Pattern { Image image; int x=0,y=0,frameH=0; };
struct State {
    bool playing=false; int hits=0;
    int update(bool start,bool end) {
        bool hit=playing?end:(start&&!end);
        hits=hit?hits+1:0;
        if(hits<3) return 0;
        hits=0; playing=!playing; return playing?1:-1;
    }
};

HINSTANCE instance;
HWND mainWindow,windowList,audioEdit,thresholdEdit,statusLabel,templateLabel;
HWND volumeSlider,volumeLabel;
HWND gameWindow=nullptr;
HFONT font,titleFont;
std::wstring baseDir,iniPath,audioPath;
std::vector<HWND> windows;
Pattern patterns[2];
State state;
bool monitoring=false,audioOpen=false,previewing=false,capturing=false,smokeTest=false;
AudioPlayer player;
double threshold=0.88;
Image selectionImage;
RECT selectionRect{};
POINT selectionOrigin{};
bool dragging=false;
int captureKind=0,displayW=0,displayH=0;


void status(const std::wstring& text) { SetWindowTextW(statusLabel,text.c_str()); }
void stopAudio() {
    player.stop(); audioOpen=false; previewing=false;
}
bool playAudio(bool beginPlayback=true) {
    stopAudio();
    if(audioPath.empty()) {status(L"请先选择本地 AIZO 音频。");return false;}
    if(!player.open(audioPath,beginPlayback)) {
        wchar_t message[180]; swprintf(message,180,L"音频解码或输出失败（0x%08lX），请检查文件和输出设备。",static_cast<unsigned long>(player.lastError));
        status(message);return false;
    }
    audioOpen=true;return true;
}
void stopAll(const std::wstring& text=L"已停止监听和音乐。") {
    monitoring=false; state=State{}; stopAudio(); status(text);
}
void loopAudio() { /* waveOut loops the decoded PCM buffer. */ }
bool saveImage(const Image& img,const std::wstring& path) {
    if(!img.valid()) return false;
    Gdiplus::Bitmap bitmap(img.w,img.h,img.w*4,PixelFormat32bppRGB,const_cast<BYTE*>(img.pixels.data()));
    CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    return bitmap.Save(path.c_str(),&png,nullptr)==Gdiplus::Ok;
}
Image loadImage(const std::wstring& path) {
    Image out;
    Gdiplus::Bitmap bitmap(path.c_str());
    if(bitmap.GetLastStatus()!=Gdiplus::Ok || bitmap.GetWidth()>1280 || bitmap.GetHeight()>2560) return out;
    Gdiplus::Rect rect(0,0,bitmap.GetWidth(),bitmap.GetHeight());
    Gdiplus::BitmapData data{};
    if(bitmap.LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppRGB,&data)!=Gdiplus::Ok) return out;
    out.w=rect.Width; out.h=rect.Height; out.pixels.resize(size_t(out.w)*out.h*4);
    for(int y=0;y<out.h;++y) memcpy(out.pixels.data()+size_t(y)*out.w*4,static_cast<BYTE*>(data.Scan0)+y*data.Stride,out.w*4);
    bitmap.UnlockBits(&data); return out;
}
Image crop(const Image& img,RECT r) {
    Image out; out.w=r.right-r.left; out.h=r.bottom-r.top;
    if(out.w<=0||out.h<=0||r.left<0||r.top<0||r.right>img.w||r.bottom>img.h) return {};
    out.pixels.resize(size_t(out.w)*out.h*4);
    for(int y=0;y<out.h;++y) memcpy(out.pixels.data()+size_t(y)*out.w*4,img.pixels.data()+(size_t(y+r.top)*img.w+r.left)*4,out.w*4);
    return out;
}
Image screenshot(HWND target) {
    Image out;
    if(!IsWindow(target)||IsIconic(target)||GetForegroundWindow()!=target) return out;
    RECT rect{}; GetClientRect(target,&rect);
    int width=rect.right,height=rect.bottom;
    if(width<320||height<180||height>width*2) return out;
    POINT pt{0,0}; ClientToScreen(target,&pt);
    int left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN);
    if(pt.x<left||pt.y<top||pt.x+width>left+GetSystemMetrics(SM_CXVIRTUALSCREEN)||pt.y+height>top+GetSystemMetrics(SM_CYVIRTUALSCREEN)) return out;
    out.w=1280; out.h=MulDiv(height,1280,width);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=out.w; info.bmiHeader.biHeight=-out.h;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);
    void* data=nullptr; HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&data,nullptr,0);
    if(bitmap) {
        HGDIOBJ old=SelectObject(memory,bitmap); SetStretchBltMode(memory,HALFTONE); SetBrushOrgEx(memory,0,0,nullptr);
        if(StretchBlt(memory,0,0,out.w,out.h,screen,pt.x,pt.y,width,height,SRCCOPY|CAPTUREBLT)) {
            out.pixels.assign(static_cast<BYTE*>(data),static_cast<BYTE*>(data)+size_t(out.w)*out.h*4);
        }
        SelectObject(memory,old); DeleteObject(bitmap);
    }
    DeleteDC(memory); ReleaseDC(nullptr,screen);
    return out;
}

// Match only near the configured ROI, with a small tolerance for layout jitter.
double match(const Image& frame,const Pattern& pattern) {
    const Image& t=pattern.image;
    if(!frame.valid()||!t.valid()||abs(frame.h-pattern.frameH)>3) return -1;
    int stride=std::max(1,int(std::sqrt(double(t.w)*t.h/1800.0)));
    std::vector<POINT> coords; std::vector<double> values;
    double sum=0;
    for(int y=0;y<t.h;y+=stride) for(int x=0;x<t.w;x+=stride) {
        coords.push_back({x,y}); values.push_back(t.gray(x,y)); sum+=values.back();
    }
    if(values.empty()) return -1;
    double mean=sum/values.size(),energy=0;
    for(auto& v:values) {v-=mean; energy+=v*v;}
    if(energy/values.size()<16) return -1;
    double best=-1;
    for(int oy=std::max(0,pattern.y-12);oy<=std::min(frame.h-t.h,pattern.y+12);++oy)
        for(int ox=std::max(0,pattern.x-12);ox<=std::min(frame.w-t.w,pattern.x+12);++ox) {
            double s=0,s2=0,cross=0;
            for(size_t i=0;i<coords.size();++i) {
                double value=frame.gray(ox+coords[i].x,oy+coords[i].y);
                s+=value; s2+=value*value; cross+=value*values[i];
            }
            double variance=s2-s*s/values.size();
            if(variance>0.0001) best=std::max(best,cross/std::sqrt(energy*variance));
        }
    return std::min(1.0,best);
}
void writeSetting(const wchar_t* section,const wchar_t* key,const std::wstring& value) {
    WritePrivateProfileStringW(section,key,value.c_str(),iniPath.c_str());
}
void updateTemplateLabel() {
    std::wstring text=L"开始模板："+std::wstring(patterns[0].image.valid()?L"已配置":L"未配置")+L"     结束模板："+(patterns[1].image.valid()?L"已配置":L"未配置");
    SetWindowTextW(templateLabel,text.c_str());
}
BOOL CALLBACK enumWindows(HWND hwnd,LPARAM) {
    if(!IsWindowVisible(hwnd)||hwnd==mainWindow||GetWindow(hwnd,GW_OWNER)) return TRUE;
    wchar_t title[512]{}; GetWindowTextW(hwnd,title,512);
    if(!title[0]) return TRUE;
    windows.push_back(hwnd); SendMessageW(windowList,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(title));
    return TRUE;
}
void refreshWindows() {
    SendMessageW(windowList,CB_RESETCONTENT,0,0); windows.clear(); EnumWindows(enumWindows,0);
    int chosen=0;
    for(size_t i=0;i<windows.size();++i) {
        wchar_t title[512]{}; GetWindowTextW(windows[i],title,512);
        if(windows[i]==gameWindow||std::wstring(title).find(L"明日方舟")!=std::wstring::npos) chosen=int(i);
    }
    SendMessageW(windowList,CB_SETCURSEL,chosen,0);
}
bool selectGame() {
    int index=int(SendMessageW(windowList,CB_GETCURSEL,0,0));
    if(index<0||index>=int(windows.size())||!IsWindow(windows[index])) {
        status(L"请刷新窗口列表并选择明日方舟电脑客户端。"); return false;
    }
    gameWindow=windows[index]; return true;
}
void restoreMain() {
    capturing=false; ShowWindow(mainWindow,SW_RESTORE); SetForegroundWindow(mainWindow);
}
LRESULT CALLBACK SelectionProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=selectionImage.w;
        info.bmiHeader.biHeight=-selectionImage.h; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        SetStretchBltMode(dc,HALFTONE);
        StretchDIBits(dc,0,0,displayW,displayH,0,0,selectionImage.w,selectionImage.h,selectionImage.pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        if(dragging) {
            HPEN pen=CreatePen(PS_SOLID,3,RGB(255,100,45)); HGDIOBJ oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc,selectionRect.left,selectionRect.top,selectionRect.right,selectionRect.bottom);
            SelectObject(dc,oldPen); SelectObject(dc,oldBrush); DeleteObject(pen);
        }
        EndPaint(hwnd,&ps); return 0;
    }
    case WM_LBUTTONDOWN:
        selectionOrigin={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; selectionRect={selectionOrigin.x,selectionOrigin.y,selectionOrigin.x,selectionOrigin.y};
        dragging=true; SetCapture(hwnd); return 0;
    case WM_MOUSEMOVE:
        if(dragging) {
            int x=std::max(0,std::min(displayW,GET_X_LPARAM(lp))),y=std::max(0,std::min(displayH,GET_Y_LPARAM(lp)));
            selectionRect={std::min<LONG>(selectionOrigin.x,x),std::min<LONG>(selectionOrigin.y,y),std::max<LONG>(selectionOrigin.x,x),std::max<LONG>(selectionOrigin.y,y)};
            InvalidateRect(hwnd,nullptr,FALSE);
        } return 0;
    case WM_LBUTTONUP:
        if(dragging) {
            dragging=false; ReleaseCapture();
            RECT r{MulDiv(selectionRect.left,1280,displayW),MulDiv(selectionRect.top,selectionImage.h,displayH),MulDiv(selectionRect.right,1280,displayW),MulDiv(selectionRect.bottom,selectionImage.h,displayH)};
            if(r.right-r.left<16||r.bottom-r.top<12) { status(L"区域过小，请重新采集。"); DestroyWindow(hwnd); return 0; }
            Pattern p{crop(selectionImage,r),int(r.left),int(r.top),selectionImage.h};
            if(match(selectionImage,p)<0.9) { status(L"区域缺少稳定细节，请选择清晰文字或图标。"); DestroyWindow(hwnd); return 0; }
            std::wstring name=captureKind?L"end":L"start";
            if(!saveImage(p.image,baseDir+L"\\"+name+L".png")) {status(L"无法保存模板，请把程序放在可写目录。"); DestroyWindow(hwnd); return 0;}
            patterns[captureKind]=std::move(p);
            writeSetting(name.c_str(),L"x",std::to_wstring(r.left)); writeSetting(name.c_str(),L"y",std::to_wstring(r.top));
            writeSetting(name.c_str(),L"height",std::to_wstring(selectionImage.h));
            updateTemplateLabel(); status(L"模板已保存。请确保两个模板分别代表联防进行中和结束。"); DestroyWindow(hwnd);
        } return 0;
    case WM_KEYDOWN: if(wp==VK_ESCAPE) {status(L"已取消采集。"); DestroyWindow(hwnd);} return 0;
    case WM_DESTROY: selectionImage={}; restoreMain(); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void beginSelection() {
    selectionImage=screenshot(gameWindow);
    if(!selectionImage.valid()) {restoreMain(); status(L"截图失败。请保持游戏窗口完整可见，且未最小化。"); return;}
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    double scale=std::min(1.0,std::min(double(work.right-work.left-60)/selectionImage.w,double(work.bottom-work.top-100)/selectionImage.h));
    displayW=int(selectionImage.w*scale); displayH=int(selectionImage.h*scale);
    RECT rect{0,0,displayW,displayH}; AdjustWindowRect(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE);
    dragging=false;
    HWND select=CreateWindowExW(WS_EX_TOPMOST,L"AizoSelect",L"拖框选择稳定标志 · 松开保存 · Esc 取消",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        work.left+20,work.top+20,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
    if(!select) {restoreMain(); status(L"无法创建截图选择窗口。"); return;}
    ShowWindow(select,SW_SHOW); SetForegroundWindow(select); SetFocus(select);
}
void tick() {
    loopAudio();
    if(!monitoring) return;
    if(!IsWindow(gameWindow)) {stopAll(L"游戏窗口已关闭，监听和音乐已停止。"); return;}
    Image frame=screenshot(gameWindow);
    if(!frame.valid()) {state.hits=0; status(state.playing?L"播放中 · 游戏不在前台，暂停识别（音乐继续）。":L"等待游戏回到前台 · 请保持窗口完整可见。"); return;}
    double start=match(frame,patterns[0]),end=match(frame,patterns[1]);
    if(start<0||end<0) {state.hits=0; status(L"画面比例与模板不一致或模板无效，请重新采集。"); return;}
    int action=state.update(start>=threshold,end>=threshold);
    if(action==1&&!playAudio()) {monitoring=false; state={}; return;}
    if(action==-1) stopAudio();
    wchar_t text[256]; swprintf(text,256,L"%ls  |  开始 %.3f / 结束 %.3f  |  阈值 %.2f",state.playing?L"联防中 · AIZO 循环播放":L"监听中 · 等待联防开始",start,end,threshold);
    status(text);
}
HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
    HWND child=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,mainWindow,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return child;
}
void chooseAudio() {
    wchar_t path[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=mainWindow;
    dialog.lpstrFilter=L"音频文件 (*.mp3;*.wav)\0*.mp3;*.wav\0所有文件\0*.*\0"; dialog.lpstrFile=path; dialog.nMaxFile=32768;
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&dialog)) {stopAll(); audioPath=path; SetWindowTextW(audioEdit,path); writeSetting(L"settings",L"audio",audioPath);}
}
LRESULT CALLBACK MainProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        mainWindow=hwnd;
        HWND title=control(L"STATIC",L"卫戍协议 · AIZO",0,24,18,660,40); SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
        control(L"STATIC",L"电脑客户端 / 窗口识别 / 联防自动配乐",0,26,62,640,25);
        control(L"STATIC",L"1  选择明日方舟窗口",0,26,104,620,25);
        windowList=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,26,134,520,260,100);
        control(L"BUTTON",L"刷新窗口",WS_TABSTOP,558,132,126,30,101);
        control(L"STATIC",L"2  选择本地 AIZO 音频",0,26,182,620,25);
        audioEdit=control(L"EDIT",audioPath.c_str(),ES_READONLY|ES_AUTOHSCROLL|WS_BORDER,26,212,520,28);
        control(L"BUTTON",L"选择音频",WS_TABSTOP,558,210,126,32,102);
        control(L"STATIC",L"3  采集识别标志（切换到游戏截图，再拖框选择）",0,26,260,650,25);
        control(L"BUTTON",L"采集联防开始标志",WS_TABSTOP,26,292,216,36,103);
        control(L"BUTTON",L"采集联防结束标志",WS_TABSTOP,254,292,216,36,104);
        control(L"BUTTON",L"试听 / 停止",WS_TABSTOP,482,292,202,36,105);
        templateLabel=control(L"STATIC",L"",0,26,340,650,25); updateTemplateLabel();
        control(L"STATIC",L"匹配阈值",0,26,388,85,26);
        thresholdEdit=control(L"EDIT",L"0.88",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,112,386,70,28);
        control(L"STATIC",L"连续 3 帧确认 · 每 150ms 检查",0,202,388,470,25);
        control(L"BUTTON",L"开始监听",BS_DEFPUSHBUTTON|WS_TABSTOP,26,438,320,44,106);
        control(L"BUTTON",L"停止监听和音乐",WS_TABSTOP,360,438,324,44,107);
        statusLabel=control(L"STATIC",L"就绪。请配置游戏窗口、音频和两种模板。",0,26,506,658,54);
        control(L"STATIC",L"仅在游戏前台识别；切走不停止音乐。关闭游戏窗口会停止音乐。\n模板保存在程序旁。改变窗口宽高比后，请重新采集。",0,26,570,658,50);
        control(L"STATIC",L"音乐音量",0,26,638,85,26);
        volumeSlider=control(TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,112,630,480,38,108);
        SendMessageW(volumeSlider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
        SendMessageW(volumeSlider,TBM_SETPAGESIZE,0,10);
        SendMessageW(volumeSlider,TBM_SETPOS,TRUE,player.volume);
        volumeLabel=control(L"STATIC",(std::to_wstring(player.volume)+L"%").c_str(),0,610,638,74,26);
        refreshWindows(); SetTimer(hwnd,1,150,nullptr);
        if(smokeTest) SetTimer(hwnd,9,1200,nullptr);
        return 0;
    }
    case WM_HSCROLL:
        if(reinterpret_cast<HWND>(lp)==volumeSlider) {
            int volume=int(SendMessageW(volumeSlider,TBM_GETPOS,0,0));
            bool applied=player.setVolume(volume);
            SetWindowTextW(volumeLabel,(std::to_wstring(player.volume)+L"%").c_str());
            writeSetting(L"settings",L"volume",std::to_wstring(player.volume));
            if(!applied) status(L"当前音频设备无法调整音量，请检查输出设备。");
        }
        return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case 100: if(HIWORD(wp)==CBN_SELCHANGE) stopAll(L"目标窗口已更改，请重新开始监听。"); break;
        case 101: if(!monitoring) refreshWindows(); else status(L"请先停止监听，再刷新窗口。"); break;
        case 102: chooseAudio(); break;
        case 103: case 104:
            stopAll(); if(!selectGame()) break;
            captureKind=LOWORD(wp)==104; capturing=true;
            ShowWindow(mainWindow,SW_MINIMIZE); ShowWindow(gameWindow,SW_RESTORE); SetForegroundWindow(gameWindow);
            SetTimer(mainWindow,2,700,nullptr); break;
        case 105: {
            bool was=audioOpen; stopAll();
            if(!was&&playAudio()) {previewing=true; status(L"正在试听，点击“试听 / 停止”结束。");} break;
        }
        case 106: {
            stopAll(); if(!selectGame()) break;
            wchar_t value[32]{}; GetWindowTextW(thresholdEdit,value,32); wchar_t* tail=nullptr; threshold=wcstod(value,&tail);
            if(tail==value||*tail||!std::isfinite(threshold)||threshold<0.5||threshold>0.99) {status(L"阈值请输入 0.50 至 0.99 之间的数字。"); break;}
            if(!patterns[0].image.valid()||!patterns[1].image.valid()) {status(L"请先采集开始和结束两种模板。"); break;}
            if(patterns[0].frameH!=patterns[1].frameH) {status(L"两个模板的画面比例不同，请保持同一窗口比例重新采集。"); break;}
            if(!playAudio(false)) break;
            stopAudio();
            monitoring=true; status(L"监听已启动，请切回明日方舟。连续识别到联防标志后播放。");
            SetForegroundWindow(gameWindow); break;
        }
        case 107: stopAll(); break;
        } return 0;
    case WM_TIMER:
        if(wp==1&&!capturing) tick();
        else if(wp==2) {KillTimer(hwnd,2); beginSelection();}
        else if(wp==9) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: KillTimer(hwnd,1); KillTimer(hwnd,2); stopAudio(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

bool selfTest() {
    State s;
    for(int round=0;round<2;++round) {
        if(s.update(true,false)!=0||s.update(false,false)!=0) return false;
        if(s.update(true,true)!=0) return false;
        if(s.update(true,false)!=0||s.update(true,false)!=0||s.update(true,false)!=1) return false;
        if(s.update(false,true)!=0||s.update(false,true)!=0||s.update(false,true)!=-1) return false;
    }
    Image image; image.w=1280; image.h=720; image.pixels.resize(1280*720*4);
    unsigned int seed=173;
    for(auto& p:image.pixels) {seed=1664525*seed+1013904223; p=static_cast<unsigned char>(seed>>24);}
    Pattern p{crop(image,{400,300,500,340}),400,300,720};
    if(match(image,p)<0.999) return false;
    p.x=600; if(match(image,p)>0.5) return false;
    p.x=400; p.frameH=800; if(match(image,p)!=-1) return false;
    p.frameH=720; std::fill(p.image.pixels.begin(),p.image.pixels.end(),0);
    if(match(image,p)!=-1) return false;
    return true;
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,LPWSTR command,int show) {
    struct ComScope { HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED); ~ComScope(){if(SUCCEEDED(result)) CoUninitialize();} } com;
    struct MfScope { HRESULT result=MFStartup(MF_VERSION); ~MfScope(){if(SUCCEEDED(result)) MFShutdown();} } mf;
    instance=h;
    // Physical pixel coordinates for capture and window rectangles on high DPI displays.
    auto user=GetModuleHandleW(L"user32.dll");
    using SetDpi=BOOL(WINAPI*)(HANDLE);
    FARPROC address=GetProcAddress(user,"SetProcessDpiAwarenessContext");
    SetDpi dpi=nullptr;
    static_assert(sizeof(dpi)==sizeof(address),"Function pointer size mismatch");
    memcpy(&dpi,&address,sizeof(dpi));
    if(dpi) dpi(reinterpret_cast<HANDLE>(-4)); else SetProcessDPIAware();
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768);
    baseDir=path; baseDir=baseDir.substr(0,baseDir.find_last_of(L"\\/")); iniPath=baseDir+L"\\settings.ini";
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token; if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok) return 1;
    if(std::wstring(command)==L"--self-test") {bool ok=selfTest(); Gdiplus::GdiplusShutdown(token); return ok?0:2;}
    smokeTest=std::wstring(command)==L"--smoke-test";
    GetPrivateProfileStringW(L"settings",L"audio",L"",path,32768,iniPath.c_str()); audioPath=path;
    player.setVolume(int(GetPrivateProfileIntW(L"settings",L"volume",100,iniPath.c_str())));
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX),ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    for(int i=0;i<2;++i) {
        const wchar_t* name=i?L"end":L"start";
        auto& p=patterns[i]; p.image=loadImage(baseDir+L"\\"+name+L".png");
        p.x=GetPrivateProfileIntW(name,L"x",-1,iniPath.c_str()); p.y=GetPrivateProfileIntW(name,L"y",-1,iniPath.c_str());
        p.frameH=GetPrivateProfileIntW(name,L"height",0,iniPath.c_str());
        if(p.x<0||p.y<0||p.x+p.image.w>1280||p.y+p.image.h>p.frameH) p={};
    }
    font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    titleFont=CreateFontW(-28,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    WNDCLASSW cls{}; cls.hInstance=h; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    cls.lpfnWndProc=MainProc; cls.lpszClassName=L"AizoMain"; cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION); RegisterClassW(&cls);
    cls.lpfnWndProc=SelectionProc; cls.lpszClassName=L"AizoSelect"; cls.hCursor=LoadCursorW(nullptr,IDC_CROSS); RegisterClassW(&cls);
    RECT rect{0,0,712,690}; AdjustWindowRect(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
    HWND hwnd=CreateWindowExW(0,L"AizoMain",L"卫戍协议 AIZO 播放器",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,h,nullptr);
    if(!hwnd) {Gdiplus::GdiplusShutdown(token); return 3;}
    ShowWindow(hwnd,show); UpdateWindow(hwnd);
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0) {if(!IsDialogMessageW(hwnd,&msg)) {TranslateMessage(&msg); DispatchMessageW(&msg);}}
    DeleteObject(font); DeleteObject(titleFont); Gdiplus::GdiplusShutdown(token); return 0;
}


