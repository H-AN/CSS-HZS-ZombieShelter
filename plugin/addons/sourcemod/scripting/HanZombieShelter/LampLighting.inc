// One audited constant lightstyle for Shelter lamps. Never overwrite environment
// style 0, HZS style 1, or map switchable style 33. Styles are global engine state.
int gLampStyleIndex=-1,gLampStyleTable=INVALID_STRING_TABLE,gLampPreviousBytes;
bool gLampStyleAttempted,gLampStyleOwned;
char gLampPreviousPattern[256];
int gLampCRC32Table[256];
bool gLampCRC32Ready;

bool LampAuditFailure(const char[] reason)
{
    LogError("[Shelter] Independent lamp BSP audit rejected: %s; lamps remain off with zero load",reason);
    return false;
}

bool LampAuditInteger(KeyValues audit,const char[] key,int &value)
{
    char text[32];audit.GetString(key,text,sizeof(text));
    return ParseLevelInteger(text,value);
}

bool LampAuditCRCValid(const char[] value)
{
    if(strlen(value)!=8)return false;
    for(int i=0;i<8;i++)
        if(!((value[i]>='0' && value[i]<='9') || (value[i]>='a' && value[i]<='f')
            || (value[i]>='A' && value[i]<='F')))return false;
    return true;
}

bool LampFileCRC32(File file,int offset,int length,char[] result,int resultLength)
{
    if(!gLampCRC32Ready)
    {
        for(int i=0;i<256;i++)
        {
            int item=i;
            for(int bit=0;bit<8;bit++)item=(item>>>1)^((item&1) ? 0xedb88320 : 0);
            gLampCRC32Table[i]=item;
        }
        gLampCRC32Ready=true;
    }
    if(!file.Seek(offset,SEEK_SET))return false;
    int crc=-1,buffer[1024],remaining=length;
    while(remaining>0)
    {
        int count=remaining<sizeof(buffer) ? remaining : sizeof(buffer);
        if(file.Read(buffer,count,1)!=count)return false;
        for(int i=0;i<count;i++)crc=(crc>>>8)^gLampCRC32Table[(crc^buffer[i])&255];
        remaining-=count;
    }
    Format(result,resultLength,"%08x",~crc);return true;
}

bool LoadLampStaticCandidates(int &allowed)
{
    allowed=0;
    char map[PLATFORM_MAX_PATH],path[PLATFORM_MAX_PATH];GetCurrentMap(map,sizeof(map));
    Format(path,sizeof(path),"maps/%s.bsp",map);
    int bspBytes=FileSize(path,true);
    if(bspBytes<1036 || bspBytes>536870912)return LampAuditFailure("BSP length outside supported 1036..536870912 bytes");
    File file=OpenFile(path,"rb",true);
    if(file==null)return LampAuditFailure("cannot open current BSP");
    int magic,version;
    if(!file.ReadInt32(magic) || !file.ReadInt32(version) || magic!=0x50534256 || version!=20)
    {delete file;return LampAuditFailure("unsupported or truncated VBSP header (requires version 20)");}
    // Read every header entry, validating subtraction before addition to avoid
    // integer overflow. Only entities/faces/HDR faces require content reads.
    int selected[3][4];
    for(int index=0;index<64;index++)
    {
        int entry[4];bool valid=true;
        for(int field=0;field<4;field++)if(!file.ReadInt32(entry[field]))valid=false;
        if(!valid || entry[0]<0 || entry[1]<0 || entry[0]>bspBytes || entry[1]>bspBytes-entry[0])
        {delete file;return LampAuditFailure("invalid/truncated BSP lump boundary");}
        int slot=index==0 ? 0 : (index==7 ? 1 : (index==58 ? 2 : -1));
        if(slot<0)continue;
        if(entry[1]>16777216 || entry[3]<0 || entry[3]>16777216
            || (entry[1]>0 && entry[0]<1036)
            || (slot>0 && (entry[2]<0 || entry[2]>1 || (entry[3]>0 ? entry[3] : entry[1])%56!=0)))
        {delete file;return LampAuditFailure("unsupported entity/face lump version, size or stride");}
        for(int field=0;field<4;field++)selected[slot][field]=entry[field];
    }
    if(selected[0][1]<1 || (selected[1][1]==0 && selected[2][1]==0))
    {delete file;return LampAuditFailure("required entity/face lumps are empty");}
    BuildPath(Path_SM,path,sizeof(path),"configs/HanZombieShelter/lightstyle_audits/%s.cfg",map);
    KeyValues audit=new KeyValues("ShelterLightstyleAudit");
    if(!audit.ImportFromFile(path))
    {delete audit;delete file;return LampAuditFailure("missing/unreadable lightstyle audit cache; run audit_bsp_lightstyles.py for this BSP");}
    int value;char cachedMap[PLATFORM_MAX_PATH];audit.GetString("map",cachedMap,sizeof(cachedMap));
    bool valid=StrEqual(map,cachedMap)
        && LampAuditInteger(audit,"schema_version",value) && value==1
        && LampAuditInteger(audit,"bsp_version",value) && value==version
        && LampAuditInteger(audit,"bsp_bytes",value) && value==bspBytes
        && LampAuditInteger(audit,"scan_complete",value) && value==1
        && LampAuditInteger(audit,"face_stride",value) && value==56
        && LampAuditInteger(audit,"face_style_offset",value) && value==16;
    if(!valid){delete audit;delete file;return LampAuditFailure("cache schema/header/scan contract mismatch");}
    for(int style=13;style<=15;style++)
    {
        char key[32];Format(key,sizeof(key),"referenced_%d",style);
        if(!LampAuditInteger(audit,key,value) || (value!=0 && value!=1))
        {delete audit;delete file;return LampAuditFailure("cache candidate flags are missing or malformed");}
        if(value==0)allowed|=1<<(style-13);
    }
    if(!audit.JumpToKey("Lumps"))
    {delete audit;delete file;return LampAuditFailure("cache lacks lump fingerprints");}
    static const char fields[][]={"offset","stored_bytes","version","fourcc"};
    for(int slot=0;slot<3;slot++)
    {
        char key[12];IntToString(slot==0 ? 0 : (slot==1 ? 7 : 58),key,sizeof(key));
        if(!audit.JumpToKey(key))
        {delete audit;delete file;return LampAuditFailure("cache lacks a required lump fingerprint");}
        for(int field=0;field<4;field++)
            if(!LampAuditInteger(audit,fields[field],value) || value!=selected[slot][field])
            {delete audit;delete file;return LampAuditFailure("cache lump metadata does not match current BSP");}
        char expected[16],actual[16];audit.GetString("crc32",expected,sizeof(expected));
        if(!LampAuditCRCValid(expected) || !LampFileCRC32(file,selected[slot][0],selected[slot][1],actual,sizeof(actual))
            || !StrEqual(expected,actual,false))
        {delete audit;delete file;return LampAuditFailure("cache lump CRC32 does not match current BSP");}
        audit.GoBack();
    }
    delete audit;delete file;
    if(allowed==0)return LampAuditFailure("BSP statically references all candidate lightstyles 13/14/15");
    return true;
}

bool LampStylePatternMatches(int table,int style,const char[] pattern,int bytes)
{
    if(table==INVALID_STRING_TABLE || style<0 || style>=GetStringTableNumStrings(table)
        || GetStringTableDataLength(table,style)!=bytes)return false;
    char current[256];GetStringTableData(table,style,current,sizeof(current));
    return StrEqual(current,pattern);
}

bool LampStyleHasOtherConsumers(int style)
{
    for(int entity=MaxClients+1;entity<GetMaxEntities();entity++)
    {
        if(!IsValidEntity(entity))continue;
        char classname[64];GetEntityClassname(entity,classname,sizeof(classname));
        int found=-1;
        if(StrEqual(classname,"light_dynamic") && HasEntProp(entity,Prop_Send,"m_LightStyle"))
            found=GetEntProp(entity,Prop_Send,"m_LightStyle");
        else if(StrContains(classname,"light")==0 && HasEntProp(entity,Prop_Data,"m_iStyle"))
            found=GetEntProp(entity,Prop_Data,"m_iStyle");
        if(found!=style)continue;
        bool ours;
        for(int b=0;b<MAX_BUILDINGS;b++)
            if(gBuildings[b].used && gBuildings[b].kind==Kind_Light && Resolve(gBuildings[b].effectRef)==entity)
            {ours=true;break;}
        if(!ours)return true;
    }
    return false;
}

bool LampLightingReady()
{
    if(!gLampStyleOwned)return false;
    int table=FindStringTable("lightstyles");
    if(table==gLampStyleTable && LampStylePatternMatches(table,gLampStyleIndex,"m",2))return true;
    // A competing writer owns its new value. Stop our lights, never fight it.
    gLampStyleOwned=false;
    LogError("[Shelter] Lamp style %d ownership lost; lamps disabled without rewriting another writer's pattern",gLampStyleIndex);
    return false;
}

bool InitializeLampLighting()
{
    if(gLampStyleAttempted)return LampLightingReady();
    gLampStyleAttempted=true;
    int allowed;
    if(!LoadLampStaticCandidates(allowed))return false;
    PropFieldType type;int bits;
    if(FindSendPropInfo("CDynamicLight","m_LightStyle",type,bits)<0 || bits!=4)
    {
        LogError("[Shelter] Independent lamp style requires verified 4-bit CDynamicLight style; lamps remain off with zero load");
        return false;
    }
    int table=FindStringTable("lightstyles");
    if(table==INVALID_STRING_TABLE || GetStringTableNumStrings(table)<16)
    {
        LogError("[Shelter] Independent lamp style table unavailable; lamps remain off with zero load");
        return false;
    }
    for(int style=13;style<=15;style++)
    {
        if((allowed&(1<<(style-13)))==0)continue;
        char key[16],expected[16];ReadStringTable(table,style,key,sizeof(key));IntToString(style,expected,sizeof(expected));
        if(!StrEqual(key,expected) || GetStringTableDataLength(table,style)!=0 || LampStyleHasOtherConsumers(style))continue;
        gLampStyleIndex=style;gLampStyleTable=table;gLampPreviousBytes=GetStringTableDataLength(table,style);
        GetStringTableData(table,style,gLampPreviousPattern,sizeof(gLampPreviousPattern));
        SetLightStyle(style,"m");
        gLampStyleOwned=LampStylePatternMatches(table,style,"m",2);
        if(!gLampStyleOwned)
        {
            LogError("[Shelter] Independent lamp style %d readback failed; no retry or replacement style is written",style);
            return false;
        }
        LogMessage("[Shelter] Lamp style %d owned with constant m; previous userdata bytes=%d; day/off phases consume no lamp power",style,gLampPreviousBytes);
        return true;
    }
    LogError("[Shelter] Audited lamp style candidates 13/14/15 are occupied; lamps remain off with zero load; other writers retained");
    return false;
}

void ReleaseLampLighting()
{
    if(gLampStyleOwned)
    {
        int table=FindStringTable("lightstyles");
        if(table==gLampStyleTable && LampStylePatternMatches(table,gLampStyleIndex,"m",2)
            && !LampStyleHasOtherConsumers(gLampStyleIndex))
        {
            bool locked=LockStringTables(false);
            SetStringTableData(table,gLampStyleIndex,gLampPreviousPattern,gLampPreviousBytes);
            LockStringTables(locked);
            LogMessage("[Shelter] Lamp style %d restored to its original userdata (%d bytes)",gLampStyleIndex,gLampPreviousBytes);
        }
        else LogMessage("[Shelter] Lamp style %d not restored: value/consumer changed; external state retained",gLampStyleIndex);
    }
    gLampStyleOwned=false;
}

void ResetLampLighting()
{
    // Previous map is released by OnMapEnd. Do not restore into a new map table.
    gLampStyleIndex=-1;gLampStyleTable=INVALID_STRING_TABLE;gLampPreviousBytes=0;
    gLampStyleAttempted=false;gLampStyleOwned=false;gLampPreviousPattern[0]='\0';
}

bool LampNightPowerActive()
{
    return Running() && gPhase==Phase_Night && LampLightingReady();
}
