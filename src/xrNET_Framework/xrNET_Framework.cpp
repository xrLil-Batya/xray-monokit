#include "pch.h"

#include "xrNET_Framework.h"

using namespace System;
using namespace System::Web;
using namespace System::Net;

bool HasWebFile(const wchar_t* web_path)
{
    String^ path = gcnew String(web_path);
    auto webRequest = HttpWebRequest::Create(path);
    webRequest->Method = "HEAD";
    try
    {
        webRequest->GetResponse()->Close();
        return true;
    }
    catch (...)
    {
        return false;
    }
}