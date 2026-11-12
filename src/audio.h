#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

class AudioPlayer {
public:
    HWAVEOUT device=nullptr;
    WAVEHDR header{};
    WAVEFORMATEX format{};
    std::vector<BYTE> pcm;
    std::wstring cached;
    HRESULT lastError=S_OK;
    bool prepared=false;
    int volume=100;
    bool setVolume(int percent) {
        volume=std::max(0,std::min(100,percent));
        if(!device) return true;
        DWORD level=static_cast<DWORD>((volume*65535u+50u)/100u);
        MMRESULT result=waveOutSetVolume(device,level|(level<<16));
        if(result) lastError=HRESULT_FROM_WIN32(result);
        return result==MMSYSERR_NOERROR;
    }
    void stop() {
        if(device) {
            waveOutReset(device);
            if(prepared) waveOutUnprepareHeader(device,&header,sizeof(header));
            waveOutClose(device);device=nullptr;prepared=false;
        }
    }
    bool open(const std::wstring& path,bool play) {
        stop();
        if(cached!=path || pcm.empty()) {
            pcm.clear();cached.clear();
            IMFSourceReader* reader=nullptr; IMFMediaType* type=nullptr;
            HMODULE dll=LoadLibraryW(L"mfreadwrite.dll");
            using CreateReader=HRESULT(WINAPI*)(LPCWSTR,IMFAttributes*,IMFSourceReader**);
            CreateReader create=nullptr;
            FARPROC addr=dll?GetProcAddress(dll,"MFCreateSourceReaderFromURL"):nullptr;
            memcpy(&create,&addr,sizeof(create));
            HRESULT hr=create?create(path.c_str(),nullptr,&reader):E_NOTIMPL;
            if(SUCCEEDED(hr)) hr=reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
            if(SUCCEEDED(hr)) hr=reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE);
            if(SUCCEEDED(hr)) hr=MFCreateMediaType(&type);
            if(SUCCEEDED(hr)) hr=type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);
            if(SUCCEEDED(hr)) hr=type->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);
            if(SUCCEEDED(hr)) hr=type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);
            if(SUCCEEDED(hr)) hr=reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,type);
            if(type) {type->Release();type=nullptr;}
            if(SUCCEEDED(hr)) hr=reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&type);
            WAVEFORMATEX* wave=nullptr; UINT32 bytes=0;
            if(SUCCEEDED(hr)) hr=MFCreateWaveFormatExFromMFMediaType(type,&wave,&bytes);
            if(SUCCEEDED(hr)) {format=*wave;format.cbSize=0;format.wFormatTag=WAVE_FORMAT_PCM;}
            CoTaskMemFree(wave);
            if(type) type->Release();
            while(SUCCEEDED(hr)) {
                DWORD flags=0; IMFSample* sample=nullptr;
                hr=reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,nullptr,&sample);
                if(SUCCEEDED(hr) && (flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) hr=E_FAIL;
                if(SUCCEEDED(hr)&&sample) {
                    IMFMediaBuffer* buffer=nullptr; hr=sample->ConvertToContiguousBuffer(&buffer);
                    if(SUCCEEDED(hr)) {
                        BYTE* data=nullptr;DWORD size=0;hr=buffer->Lock(&data,nullptr,&size);
                        if(SUCCEEDED(hr)) {
                            if(pcm.size()+size>256u*1024u*1024u) hr=E_OUTOFMEMORY;
                            else pcm.insert(pcm.end(),data,data+size);
                            buffer->Unlock();
                        }
                        buffer->Release();
                    }
                }
                if(sample) sample->Release();
                if(flags&MF_SOURCE_READERF_ENDOFSTREAM) break;
            }
            if(reader) reader->Release();
            if(dll) FreeLibrary(dll);
            if(FAILED(hr)||pcm.empty()) {pcm.clear();lastError=FAILED(hr)?hr:E_FAIL;return false;}
            cached=path;
        }
        MMRESULT result=waveOutOpen(&device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL);
        if(result) {lastError=HRESULT_FROM_WIN32(result);device=nullptr;return false;}
        if(!setVolume(volume)) {stop();return false;}
        if(!play) return true;
        header={};header.lpData=reinterpret_cast<LPSTR>(pcm.data());header.dwBufferLength=DWORD(pcm.size());
        header.dwFlags=WHDR_BEGINLOOP|WHDR_ENDLOOP;header.dwLoops=0xffffffff;
        result=waveOutPrepareHeader(device,&header,sizeof(header));prepared=result==0;
        if(!result) result=waveOutWrite(device,&header,sizeof(header));
        if(result) {lastError=HRESULT_FROM_WIN32(result);stop();return false;}
        return true;
    }
};
