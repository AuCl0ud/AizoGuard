// Integration tests use temporary images and silent WAV audio, never game files.
#include "../src/main.cpp"
#include <iostream>

int main() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED); MFStartup(MF_VERSION);
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token=0;
    if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok) return 1;
    bool ok=selfTest();
    wchar_t temp[MAX_PATH]{},name[MAX_PATH]{};
    GetTempPathW(MAX_PATH,temp); GetTempFileNameW(temp,L"azg",0,name);
    std::wstring png=std::wstring(name)+L".png",wav=std::wstring(name)+L".wav";
    Image img; img.w=80; img.h=40; img.pixels.resize(80*40*4);
    for(size_t i=0;i<img.pixels.size();++i) img.pixels[i]=static_cast<BYTE>(i*31);
    ok=saveImage(img,png)&&ok;
    Image loaded=loadImage(png);
    ok=loaded.valid()&&loaded.w==img.w&&loaded.h==img.h&&ok;
    if(loaded.valid()) for(int y=0;y<img.h;++y) for(int x=0;x<img.w;++x)
        if(std::abs(loaded.gray(x,y)-img.gray(x,y))>0.001) ok=false;
    // One second of silence: mono, 8000 Hz, 16 bit PCM.
    std::vector<BYTE> bytes(44+16000,0);
    auto u16=[&](int at,unsigned value){bytes[at]=value&255; bytes[at+1]=(value>>8)&255;};
    auto u32=[&](int at,unsigned value){u16(at,value);u16(at+2,value>>16);};
    memcpy(bytes.data(),"RIFF",4);u32(4,unsigned(bytes.size()-8));memcpy(bytes.data()+8,"WAVEfmt ",8);
    u32(16,16);u16(20,1);u16(22,1);u32(24,8000);u32(28,16000);u16(32,2);u16(34,16);
    memcpy(bytes.data()+36,"data",4);u32(40,16000);
    HANDLE file=CreateFileW(wav.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    DWORD written=0;
    if(file==INVALID_HANDLE_VALUE) ok=false;
    else {ok=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&ok;CloseHandle(file);}
    audioPath=wav;
    bool audio=playAudio(); ok=audio&&ok;
    if(audio) {
        for(int level : {0,35,100}) {
            DWORD actual=0,expected=DWORD((level*65535u+50u)/100u);
            ok=player.setVolume(level)&&waveOutGetVolume(player.device,&actual)==0&&
                std::abs(int(LOWORD(actual))-int(expected))<=256&&
                std::abs(int(HIWORD(actual))-int(expected))<=256&&ok;
        }
        player.setVolume(35);
        Sleep(1200);
        MMTIME position{};position.wType=TIME_BYTES;
        ok=waveOutGetPosition(player.device,&position,sizeof(position))==0&&position.u.cb>0&&ok;
        ok=(player.header.dwFlags&WHDR_INQUEUE)&&!(player.header.dwFlags&WHDR_DONE)&&ok;
    }
    stopAudio(); ok=!audioOpen&&ok;
    ok=playAudio(false)&&ok;
    DWORD reopened=0;
    ok=player.volume==35&&waveOutGetVolume(player.device,&reopened)==0&&
        std::abs(int(LOWORD(reopened))-22937)<=256&&ok;
    stopAudio();
    iniPath=std::wstring(name)+L".ini";
    writeSetting(L"settings",L"volume",std::to_wstring(player.volume));
    ok=GetPrivateProfileIntW(L"settings",L"volume",100,iniPath.c_str())==35&&ok;
    DeleteFileW(iniPath.c_str());
    std::cout<<"Volume live adjustment, reopen and saved setting: "<<(ok?"PASS":"FAIL")<<std::endl;
    wchar_t realPath[32768]{}; GetFullPathNameW(L"King Gnu - AIZO.mp3",32768,realPath,nullptr);
    audioPath=realPath;
    bool mp3=playAudio(false);
    if(mp3) {
        mp3=player.pcm.size()>1000000;
        std::cout<<"Decoded PCM bytes: "<<player.pcm.size()<<std::endl;
        // Verify actual decoded music can be queued, muted before writing.
        waveOutSetVolume(player.device,0);
        player.header={};player.header.lpData=reinterpret_cast<LPSTR>(player.pcm.data());
        player.header.dwBufferLength=DWORD(player.pcm.size());
        player.prepared=waveOutPrepareHeader(player.device,&player.header,sizeof(player.header))==0;
        mp3=player.prepared&&waveOutWrite(player.device,&player.header,sizeof(player.header))==0&&mp3;
        Sleep(200);
        MMTIME pos{};pos.wType=TIME_BYTES;
        mp3=waveOutGetPosition(player.device,&pos,sizeof(pos))==0&&pos.u.cb>0&&mp3;
    } else std::cout<<"Audio HRESULT: "<<std::hex<<player.lastError<<std::dec<<std::endl;
    ok=mp3&&ok; stopAudio();
    std::cout<<"Actual AIZO MP3: "<<(mp3?"PASS":"FAIL")<<std::endl;
    Image startFrame=loadImage(L"build/start-frame.png"),endFrame=loadImage(L"build/end-frame.png");
    Pattern startPattern{loadImage(L"assets/start.png"),681,372,800};
    Pattern endPattern{loadImage(L"assets/end.png"),681,372,800};
    double ss=match(startFrame,startPattern),se=match(startFrame,endPattern),ee=match(endFrame,endPattern),es=match(endFrame,startPattern);
    std::cout<<"Screenshots start/end: "<<ss<<"/"<<se<<" ; "<<es<<"/"<<ee<<std::endl;
    ok=ss>=0.88&&ee>=0.88&&se<0.88&&es<0.88&&ok;
    DeleteFileW(png.c_str());DeleteFileW(wav.c_str());DeleteFileW(name);
    Gdiplus::GdiplusShutdown(token);
    MFShutdown(); CoUninitialize();
    std::cout<<(ok?"PASS: recognition, PNG round-trip, WAV playback, loop and stop":"FAIL: native integration")<<std::endl;
    return ok?0:1;
}

