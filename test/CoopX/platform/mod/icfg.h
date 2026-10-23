



#ifndef _ICFG
#define _ICFG

class ICFG
{
public:
    virtual void* InitIniPointer() = 0;
    virtual void ParseInputStream(void* iniPointer, const char* szFilename) = 0;
    virtual void GenerateToOutputStream(void* iniPointer, const char* szFilename) = 0;
    virtual const char* GetValueFrom(void* iniPointer, const char* szSection, const char* szKey) = 0;
    virtual void SetValueTo(void* iniPointer, const char* szSection, const char* szKey, const char* szValue) = 0;



    virtual bool GetValueFrom(void* iniPointer, const char* szSection, const char* szKey, char* pValue, int maxLen) = 0;
    virtual bool HasSection(void* iniPointer, const char* szSection) = 0;
    virtual bool HasKey(void* iniPointer, const char* szSection, const char* szKey) = 0;
    virtual bool HasSectionComment(void* iniPointer, const char* szSection) = 0;
    virtual bool HasKeyComment(void* iniPointer, const char* szSection, const char* szKey) = 0;
};

extern ICFG* icfg;
inline ICFG* GetCFGInterface()
{
    return icfg;
}

#endif