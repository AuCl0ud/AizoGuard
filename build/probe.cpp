#include <windows.h>
#include <mmsystem.h>
#include <iostream>
int main(){wchar_t path[32768]; GetFullPathNameW(L"King Gnu - AIZO.mp3",32768,path,0); std::wstring cmd=L"open \""+std::wstring(path)+L"\" alias probe"; auto e=mciSendStringW(cmd.c_str(),0,0,0); std::cout<<"MCI open error="<<e<<std::endl; mciSendStringW(L"close probe",0,0,0);}
