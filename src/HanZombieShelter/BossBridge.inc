// Match the existing HZS Stage reader and SpawnCustomZombies without changing HZS.
// Native CustomZombies[512], entries[32][64], and key:count are byte-bounded.
#define BOSS_NATIVE_CUSTOM_BYTES 512
#define BOSS_NATIVE_CUSTOM_ENTRIES 32
#define BOSS_NATIVE_CUSTOM_KEY_BYTES 64

enum BossNativeListError
{
    BossNativeList_Ok,
    BossNativeList_TooLong,
    BossNativeList_EmptyEntry,
    BossNativeList_TooManyEntries,
    BossNativeList_EntryTooLong,
    BossNativeList_InvalidEntry,
    BossNativeList_DuplicateKey,
    BossNativeList_TrackingCapacity
}

// Positive decimal counts only. Bound before multiplication, so huge integers
// cannot wrap into a small count that would pass the shared tracking budget.
bool BossNativeParseCount(const char[] text,int &count)
{
    count=0;int length=strlen(text);if(length<1)return false;
    for(int i=0;i<length;i++)
    {
        if(text[i]<'0' || text[i]>'9')return false;
        int digit=text[i]-'0';
        if(count>(MAX_NPCS-digit)/10)return false;
        count=count*10+digit;
    }
    return count>0;
}

bool BossNativeParseEntry(const char[] entry,char[] key,int keySize,int &count)
{
    count=0;if(keySize<1)return false;key[0]='\0';
    int length=strlen(entry),colon=-1;
    if(length<3 || length>=BOSS_NATIVE_CUSTOM_KEY_BYTES)return false;
    for(int i=0;i<length;i++)
    {
        int byte=entry[i]&0xff;
        if(byte<=32 || byte==127 || entry[i]==',')return false;
        if(entry[i]==':')
        {
            if(colon!=-1)return false;
            colon=i;
        }
    }
    if(colon<1 || colon>=length-1 || colon>=keySize)return false;
    char amount[BOSS_NATIVE_CUSTOM_KEY_BYTES];
    for(int i=colon+1;i<length;i++)amount[i-colon-1]=entry[i];
    amount[length-colon-1]='\0';
    if(!BossNativeParseCount(amount,count))return false;
    for(int i=0;i<colon;i++)key[i]=entry[i];
    key[colon]='\0';return true;
}

// Pure parsing helper. Empty CSV is syntactically valid; configured Bosses then
// fail the semantic coverage check below. Reject duplicates instead of silently
// summing entries that an administrator may have copied accidentally.
BossNativeListError BossNativeParseCustom(const char[] custom,
    char keys[BOSS_NATIVE_CUSTOM_ENTRIES][BOSS_NATIVE_CUSTOM_KEY_BYTES],
    int counts[BOSS_NATIVE_CUSTOM_ENTRIES],int &entryCount,int &totalCount)
{
    entryCount=0;totalCount=0;
    int length=strlen(custom);
    if(length>=BOSS_NATIVE_CUSTOM_BYTES)return BossNativeList_TooLong;
    if(length==0)return BossNativeList_Ok;
    int start=0;
    for(int cursor=0;cursor<=length;cursor++)
    {
        if(cursor<length && custom[cursor]!=',')continue;
        int entryLength=cursor-start;
        if(entryLength==0)return BossNativeList_EmptyEntry;
        if(entryCount>=BOSS_NATIVE_CUSTOM_ENTRIES)return BossNativeList_TooManyEntries;
        if(entryLength>=BOSS_NATIVE_CUSTOM_KEY_BYTES)return BossNativeList_EntryTooLong;
        char entry[BOSS_NATIVE_CUSTOM_KEY_BYTES];
        for(int i=0;i<entryLength;i++)entry[i]=custom[start+i];
        entry[entryLength]='\0';
        if(!BossNativeParseEntry(entry,keys[entryCount],BOSS_NATIVE_CUSTOM_KEY_BYTES,counts[entryCount]))
            return BossNativeList_InvalidEntry;
        for(int previous=0;previous<entryCount;previous++)
            if(StrEqual(keys[previous],keys[entryCount]))return BossNativeList_DuplicateKey;
        if(counts[entryCount]>MAX_NPCS-totalCount)return BossNativeList_TrackingCapacity;
        totalCount+=counts[entryCount];entryCount++;start=cursor+1;
    }
    return BossNativeList_Ok;
}

// Exact CSV token comparison: boss_fat must not match ordinary boss_fat_small.
bool BossNativeCSVContainsKey(const char[] csv,const char[] key)
{
    int length=strlen(csv),keyLength=strlen(key),start=0;
    if(keyLength<1)return false;
    for(int cursor=0;cursor<=length;cursor++)
    {
        if(cursor<length && csv[cursor]!=',')continue;
        if(cursor-start==keyLength)
        {
            bool match=true;
            for(int i=0;i<keyLength;i++)if(csv[start+i]!=key[i]){match=false;break;}
            if(match)return true;
        }
        start=cursor+1;
    }
    return false;
}

bool ValidateBossNativeCustomList(const char[] ordinary,const char[] custom)
{
    if(strlen(ordinary)>=BOSS_NATIVE_CUSTOM_BYTES)
        return ConfigFailure("native bridge zombie CSV exceeds HZS 511-byte limit");
    char keys[BOSS_NATIVE_CUSTOM_ENTRIES][BOSS_NATIVE_CUSTOM_KEY_BYTES];
    int counts[BOSS_NATIVE_CUSTOM_ENTRIES],entryCount,totalCount;
    BossNativeListError error=BossNativeParseCustom(custom,keys,counts,entryCount,totalCount);
    if(error!=BossNativeList_Ok)
    {
        char reason[224];
        switch(error)
        {
            case BossNativeList_TooLong:strcopy(reason,sizeof(reason),"customzombies exceeds HZS 511-byte limit");
            case BossNativeList_EmptyEntry:strcopy(reason,sizeof(reason),"customzombies contains an empty CSV entry");
            case BossNativeList_TooManyEntries:strcopy(reason,sizeof(reason),"customzombies exceeds HZS 32-entry limit");
            case BossNativeList_EntryTooLong:strcopy(reason,sizeof(reason),"customzombies key:count exceeds HZS 63-byte entry limit");
            case BossNativeList_InvalidEntry:strcopy(reason,sizeof(reason),"customzombies expects an unspaced key:positive_decimal_count within the NPC tracking limit");
            case BossNativeList_DuplicateKey:strcopy(reason,sizeof(reason),"customzombies has a duplicate key; combine its counts into one entry");
            case BossNativeList_TrackingCapacity:strcopy(reason,sizeof(reason),"customzombies total exceeds NPC tracking capacity");
            default:strcopy(reason,sizeof(reason),"invalid customzombies list");
        }
        return ConfigFailure(reason);
    }
    // Keep the existing two ordinary generations of registration headroom, but
    // reserve the actual native custom count rather than MAX_BOSSES capacity.
    if(gNativePoolSize<1 || gPopulationLimit<1 || gPopulationLimit>gNativePoolSize
        || totalCount>MAX_NPCS || gNativePoolSize>(MAX_NPCS-totalCount)/2)
        return ConfigFailure("native pool budget requires 2*initial_ordinary_count + all customzombies counts <= MAX_NPCS");

    for(int i=0;i<gBossCount;i++)
    {
        for(int previous=0;previous<i;previous++)
        {
            bool sameKey=StrEqual(gBosses[previous].key,gBosses[i].key);
            bool sameName=StrEqual(gBosses[previous].publicName,gBosses[i].publicName);
            if(sameKey!=sameName)
            {
                char reason[224];Format(reason,sizeof(reason),
                    "Bosses %s/%s have ambiguous public_name/native key mapping",gBosses[previous].id,gBosses[i].id);
                return ConfigFailure(reason);
            }
        }
        if(BossNativeCSVContainsKey(ordinary,gBosses[i].key))
            return ConfigFailure("fixed boss cannot appear in ordinary reborn pool");
        bool checked=false;
        for(int previous=0;previous<i;previous++)if(StrEqual(gBosses[previous].key,gBosses[i].key)){checked=true;break;}
        if(checked)continue;
        int required,available;
        for(int other=0;other<gBossCount;other++)if(StrEqual(gBosses[other].key,gBosses[i].key))required++;
        for(int entry=0;entry<entryCount;entry++)if(StrEqual(keys[entry],gBosses[i].key)){available=counts[entry];break;}
        // A surplus instance would otherwise fall through BindNPC into the
        // ordinary pool. A fixed Boss type must have exactly its declared count.
        if(available!=required)
        {
            char reason[224];Format(reason,sizeof(reason),
                "customzombies %s count %d must equal %d configured Boss instances",gBosses[i].key,available,required);
            return ConfigFailure(reason);
        }
    }
    return true;
}
