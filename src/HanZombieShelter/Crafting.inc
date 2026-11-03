// Crafting only settles the player's material ledger. Weapon delivery remains
// the responsibility of the configured external command's provider.
#define MAX_WEAPON_CRAFTS 256
#define MAX_MATERIAL_CRAFTS 256
#define MAX_CRAFT_STATIONS 256

enum struct WeaponCraftRecipe
{
    char id[64];
    char label[128];
    char station[64];
    int requiredLevel;
    char command[128];
    int cost[MATERIALS];
}

enum struct MaterialCraftRecipe
{
    char id[64];
    char label[128];
    int outputMaterial;
    int outputAmount;
    int cost[MATERIALS];
}

WeaponCraftRecipe gWeaponCrafts[MAX_WEAPON_CRAFTS];
MaterialCraftRecipe gMaterialCrafts[MAX_MATERIAL_CRAFTS];
int gWeaponCraftCount,gMaterialCraftCount;
char gCraftStationNames[MAX_CRAFT_STATIONS][64];
int gCraftStationMaximum[MAX_CRAFT_STATIONS],gCraftStationCount;
bool gCraftingTransaction[MAXPLAYERS+1];
int gCraftingTransactionSerial[MAXPLAYERS+1];

bool CraftingValidSectionName(const char[] name)
{
    int length=strlen(name);
    if(length<1 || length>=64 || StrContains(name,"/")>=0 || StrContains(name,":")>=0)return false;
    char trimmed[256];strcopy(trimmed,sizeof(trimmed),name);TrimString(trimmed);
    if(!StrEqual(trimmed,name))return false;
    for(int c=0;c<length;c++)if((name[c]&0xff)<32 || (name[c]&0xff)==127)return false;
    return true;
}

void CraftingSectionIDs(ConfigFile file,const char[] path,ArrayList ids)
{
    if(!Node(file,path) || !gCfg[file].GotoFirstSubKey(false))return;
    do
    {
        // Read into a larger buffer before copying into the bounded catalogue.
        char id[1024];gCfg[file].GetSectionName(id,sizeof(id));
        if(!CraftingValidSectionName(id) || gCfg[file].GetDataType(NULL_STRING)!=KvData_None)
        {LogError("[ShelterCrafting] Ignored invalid/overlong section in config=%d/%s",view_as<int>(file),path);continue;}
        ids.PushString(id);
    }while(gCfg[file].GotoNextKey(false));
}

bool CraftingUniqueSection(ArrayList ids,int entry)
{
    char id[64],other[64];ids.GetString(entry,id,sizeof(id));
    for(int i=0;i<ids.Length;i++)
    {
        if(i==entry)continue;
        ids.GetString(i,other,sizeof(other));if(StrEqual(id,other))return false;
    }
    return true;
}

bool CraftingReadText(ConfigFile file,const char[] path,const char[] key,char[] value,int length,bool &found)
{
    value[0]=0;found=false;
    if(!Node(file,path) || gCfg[file].GetDataType(key)==KvData_None)return true;
    char text[1024];gCfg[file].GetString(key,text,sizeof(text));found=true;
    if(strlen(text)>=length)return false;
    for(int c=0;text[c];c++)if((text[c]&0xff)<32 || (text[c]&0xff)==127)return false;
    strcopy(value,length,text);return true;
}

bool CraftingEnabled(ConfigFile file,const char[] path,bool &enabled)
{
    // A subsection named enabled is malformed, rather than an omitted flag.
    if(Node(file,path) && gCfg[file].GetDataType("enabled")==KvData_None && gCfg[file].JumpToKey("enabled"))return false;
    char text[8];bool found;
    if(!CraftingReadText(file,path,"enabled",text,sizeof(text),found))return false;
    if(!found){enabled=true;return true;}
    if(!StrEqual(text,"0") && !StrEqual(text,"1"))return false;
    enabled=StrEqual(text,"1");return true;
}

bool CraftingRecipeFields(ConfigFile file,const char[] path,bool material)
{
    if(!Node(file,path) || !gCfg[file].GotoFirstSubKey(false))return false;
    StringMap seen=new StringMap();bool valid=true;
    do
    {
        char key[256];int ignored;gCfg[file].GetSectionName(key,sizeof(key));
        bool scalar=StrEqual(key,"enabled") || StrEqual(key,"display_name")
            || (!material && (StrEqual(key,"required_station_type") || StrEqual(key,"required_level") || StrEqual(key,"command")));
        bool section=StrEqual(key,"cost") || (material && StrEqual(key,"output"));
        if((!scalar && !section) || seen.GetValue(key,ignored)
            || (scalar && gCfg[file].GetDataType(NULL_STRING)==KvData_None)
            || (section && gCfg[file].GetDataType(NULL_STRING)!=KvData_None))
        {valid=false;break;}
        seen.SetValue(key,1);
    }while(gCfg[file].GotoNextKey(false));
    delete seen;return valid;
}

bool ReadCraftingCost(ConfigFile file,const char[] path,int cost[MATERIALS])
{
    for(int m=0;m<MATERIALS;m++)cost[m]=0;
    if(!Node(file,path) || !gCfg[file].GotoFirstSubKey(false))return false;
    bool seen[MATERIALS],positive;
    do
    {
        char key[256],text[128];int amount;gCfg[file].GetSectionName(key,sizeof(key));
        int material=FindMaterial(key);
        if(material<0 || material>=MATERIALS || seen[material] || gCfg[file].GetDataType(NULL_STRING)==KvData_None)return false;
        gCfg[file].GetString(NULL_STRING,text,sizeof(text));
        if(strlen(text)>=sizeof(text)-1 || !ParseLevelInteger(text,amount))return false;
        seen[material]=true;cost[material]=amount;if(amount>0)positive=true;
    }while(gCfg[file].GotoNextKey(false));
    return positive;
}

bool CraftingReadOutput(ConfigFile file,const char[] path,int &material,int &amount)
{
    material=-1;amount=0;
    if(!Node(file,path) || !gCfg[file].GotoFirstSubKey(false))return false;
    bool materialFound,amountFound;
    do
    {
        char key[256],text[128];gCfg[file].GetSectionName(key,sizeof(key));
        if(gCfg[file].GetDataType(NULL_STRING)==KvData_None)return false;
        gCfg[file].GetString(NULL_STRING,text,sizeof(text));
        if(strlen(text)>=sizeof(text)-1)return false;
        if(StrEqual(key,"material") && !materialFound)
        {materialFound=true;material=FindMaterial(text);if(material<0)return false;}
        else if(StrEqual(key,"amount") && !amountFound)
        {amountFound=true;if(!ParseLevelInteger(text,amount) || amount<1)return false;}
        else return false;
    }while(gCfg[file].GotoNextKey(false));
    return materialFound && amountFound;
}

bool CraftingCommandValid(const char[] command)
{
    if(!command[0] || strlen(command)>=128)return false;
    // One registered command name, without parameters or command separators.
    // The external command is passed as data to FakeClientCommand's format.
    for(int c=0;command[c];c++)
        if(!((command[c]>='a' && command[c]<='z') || (command[c]>='A' && command[c]<='Z')
            || (command[c]>='0' && command[c]<='9') || command[c]=='_' || command[c]=='-' || command[c]=='.'))return false;
    return true;
}

bool CraftingStationHasBuildRecipe(const char[] type)
{
    for(int r=0;r<gRecipeCount;r++)
        if(gRecipes[r].kind==Kind_Workbench && gRecipes[r].file==C_Workbenches && StrEqual(gRecipes[r].type,type))return true;
    return false;
}

void ReadCraftingStations()
{
    gCraftStationCount=0;
    ArrayList ids=new ArrayList(ByteCountToCells(64));CraftingSectionIDs(C_Workbenches,"Types",ids);
    for(int i=0;i<ids.Length;i++)
    {
        char type[64],path[128];ids.GetString(i,type,sizeof(type));
        if(!CraftingUniqueSection(ids,i))
        {LogError("[ShelterCrafting] Duplicate workbench name disabled: %s",type);continue;}
        Format(path,sizeof(path),"Types/%s",type);bool enabled;
        if(!CraftingEnabled(C_Workbenches,path,enabled))
        {LogError("[ShelterCrafting] Invalid enabled value for workbench %s",type);continue;}
        if(!enabled || !CraftingStationHasBuildRecipe(type))continue;
        int maximum=ProfileMaximumLevel(C_Workbenches,path);
        if(maximum<1 || gCraftStationCount>=MAX_CRAFT_STATIONS)continue;
        strcopy(gCraftStationNames[gCraftStationCount],sizeof(gCraftStationNames[]),type);
        gCraftStationMaximum[gCraftStationCount++]=maximum;
    }
    delete ids;
}

int FindCraftingStationType(const char[] type)
{
    for(int s=0;s<gCraftStationCount;s++)if(StrEqual(gCraftStationNames[s],type))return s;
    return -1;
}

void ReadWeaponCrafting()
{
    gWeaponCraftCount=0;
    ArrayList ids=new ArrayList(ByteCountToCells(64));CraftingSectionIDs(C_WeaponCrafting,"Recipes",ids);
    for(int entry=0;entry<ids.Length;entry++)
    {
        WeaponCraftRecipe recipe;ids.GetString(entry,recipe.id,sizeof(recipe.id));
        char path[192],costPath[192];Format(path,sizeof(path),"Recipes/%s",recipe.id);bool enabled,found;
        bool valid=CraftingUniqueSection(ids,entry) && CraftingRecipeFields(C_WeaponCrafting,path,false)
            && CraftingEnabled(C_WeaponCrafting,path,enabled);
        if(valid && !enabled)continue;
        valid=valid && CraftingReadText(C_WeaponCrafting,path,"display_name",recipe.label,sizeof(recipe.label),found);
        if(!recipe.label[0])strcopy(recipe.label,sizeof(recipe.label),recipe.id);
        valid=valid && CraftingReadText(C_WeaponCrafting,path,"required_station_type",recipe.station,sizeof(recipe.station),found);
        if(valid && !found && gCraftStationCount>0)strcopy(recipe.station,sizeof(recipe.station),gCraftStationNames[0]);
        int station=FindCraftingStationType(recipe.station);
        char levelText[32];
        valid=valid && station>=0 && CraftingReadText(C_WeaponCrafting,path,"required_level",levelText,sizeof(levelText),found);
        recipe.requiredLevel=1;
        if(valid && found)valid=ParseLevelInteger(levelText,recipe.requiredLevel) && recipe.requiredLevel>=1;
        if(valid)valid=recipe.requiredLevel<=gCraftStationMaximum[station];
        valid=valid && CraftingReadText(C_WeaponCrafting,path,"command",recipe.command,sizeof(recipe.command),found)
            && found && CraftingCommandValid(recipe.command);
        Format(costPath,sizeof(costPath),"%s/cost",path);
        valid=valid && ReadCraftingCost(C_WeaponCrafting,costPath,recipe.cost);
        if(!valid)
        {LogError("[ShelterCrafting] Disabled weapon recipe %s: invalid/duplicate fields, station, level, command or cost",recipe.id);continue;}
        if(gWeaponCraftCount>=MAX_WEAPON_CRAFTS)
        {LogError("[ShelterCrafting] Weapon recipe capacity %d exceeded",MAX_WEAPON_CRAFTS);break;}
        gWeaponCrafts[gWeaponCraftCount++]=recipe;
    }
    delete ids;
}

void ReadMaterialCrafting()
{
    gMaterialCraftCount=0;
    ArrayList ids=new ArrayList(ByteCountToCells(64));CraftingSectionIDs(C_MaterialCrafting,"Recipes",ids);
    for(int entry=0;entry<ids.Length;entry++)
    {
        MaterialCraftRecipe recipe;ids.GetString(entry,recipe.id,sizeof(recipe.id));
        char path[192],child[192];Format(path,sizeof(path),"Recipes/%s",recipe.id);bool enabled,found;
        bool valid=CraftingUniqueSection(ids,entry) && CraftingRecipeFields(C_MaterialCrafting,path,true)
            && CraftingEnabled(C_MaterialCrafting,path,enabled);
        if(valid && !enabled)continue;
        valid=valid && CraftingReadText(C_MaterialCrafting,path,"display_name",recipe.label,sizeof(recipe.label),found);
        if(!recipe.label[0])strcopy(recipe.label,sizeof(recipe.label),recipe.id);
        Format(child,sizeof(child),"%s/cost",path);valid=valid && ReadCraftingCost(C_MaterialCrafting,child,recipe.cost);
        Format(child,sizeof(child),"%s/output",path);
        valid=valid && CraftingReadOutput(C_MaterialCrafting,child,recipe.outputMaterial,recipe.outputAmount);
        if(!valid)
        {LogError("[ShelterCrafting] Disabled material recipe %s: invalid/duplicate fields, input or output",recipe.id);continue;}
        if(gMaterialCraftCount>=MAX_MATERIAL_CRAFTS)
        {LogError("[ShelterCrafting] Material recipe capacity %d exceeded",MAX_MATERIAL_CRAFTS);break;}
        gMaterialCrafts[gMaterialCraftCount++]=recipe;
    }
    delete ids;
}

bool LoadCraftingConfig()
{
    for(int client=0;client<=MAXPLAYERS;client++)
    {gCraftingTransaction[client]=false;gCraftingTransactionSerial[client]=0;}
    ReadCraftingStations();ReadWeaponCrafting();ReadMaterialCrafting();
    return true; // Missing optional catalogues never disable an older install.
}

bool CraftingStationInteractable(int client,int building)
{
    return Running() && LiveHuman(client) && !IsFakeClient(client) && !IsClientSourceTV(client) && !IsClientReplay(client)
        && BuildingOperational(building) && gBuildings[building].kind==Kind_Workbench
        && gBuildings[building].file==C_Workbenches && gBuildings[building].configRevision==gConfigRevision
        && FindCraftingStationType(gBuildings[building].type)>=0
        && Resolve(gBuildings[building].ref)>0 && BuildCoreAlive(gBuildings[building].buildCore)
        && BuildingInUseRange(client,building);
}

bool CraftingStationUsable(int client,int building)
{
    return CraftingStationInteractable(client,building) && BuildingPowered(building);
}

bool HasCraftMaterials(int client,const int cost[MATERIALS])
{
    if(client<1 || client>MaxClients || !IsClientInGame(client))return false;
    for(int m=0;m<gMaterialCount;m++)if(cost[m]<0 || gPlayerInventory[client][m]<cost[m])return false;
    return true;
}

bool WeaponCraftAllowed(int building,int recipe)
{
    return recipe>=0 && recipe<gWeaponCraftCount && BuildingOperational(building)
        && gBuildings[building].kind==Kind_Workbench && gBuildings[building].file==C_Workbenches
        && gBuildings[building].configRevision==gConfigRevision
        && StrEqual(gBuildings[building].type,gWeaponCrafts[recipe].station)
        && gBuildings[building].level>=gWeaponCrafts[recipe].requiredLevel;
}

bool WeaponCraftCommandExists(int recipe)
{
    return recipe>=0 && recipe<gWeaponCraftCount && CraftingCommandValid(gWeaponCrafts[recipe].command)
        && CommandExists(gWeaponCrafts[recipe].command);
}

bool MaterialCraftHasRoom(int client,int recipe)
{
    if(recipe<0 || recipe>=gMaterialCraftCount || !HasCraftMaterials(client,gMaterialCrafts[recipe].cost))return false;
    int output=gMaterialCrafts[recipe].outputMaterial,amount=gMaterialCrafts[recipe].outputAmount;
    if(output<0 || output>=gMaterialCount || amount<1)return false;
    int maximum=gInventoryCap>0 ? gInventoryCap : LEVEL_CELL_LIMIT;
    int after=gPlayerInventory[client][output]-gMaterialCrafts[recipe].cost[output];
    // Subtract input first: same-material recipes may free output space.
    return after>=0 && after<=maximum && amount<=maximum-after;
}

bool CraftingTransactionBusy(int client)
{
    return gCraftingTransaction[client] && gCraftingTransactionSerial[client]==GetClientSerial(client);
}

void BeginCraftingTransaction(int client,int serial)
{
    gCraftingTransactionSerial[client]=serial;gCraftingTransaction[client]=true;
}

void EndCraftingTransaction(int client,int serial)
{
    // A command can synchronously disconnect its caller. A newly connected
    // player reusing that slot owns a different serial and a different lock.
    if(gCraftingTransactionSerial[client]!=serial)return;
    gCraftingTransaction[client]=false;gCraftingTransactionSerial[client]=0;
}

bool TryCraftWeapon(int client,int building,int recipe)
{
    if(client<1 || client>MaxClients || !IsClientInGame(client))return false;
    if(!CraftingStationUsable(client,building))
    {Tell(client,"合成台已失效、未供电或没有近距离瞄准，请重新按 E。");return false;}
    if(CraftingTransactionBusy(client) || !WeaponCraftAllowed(building,recipe))
    {Tell(client,"当前合成台的类型或等级不满足这条武器配方。");return false;}
    if(!WeaponCraftCommandExists(recipe))
    {Tell(client,"这件武器的外部指令当前不可用，未扣除材料。");return false;}
    if(!HasCraftMaterials(client,gWeaponCrafts[recipe].cost))
    {Tell(client,"个人背包材料不足，未扣除材料。");return false;}
    char command[128],label[128];strcopy(command,sizeof(command),gWeaponCrafts[recipe].command);
    strcopy(label,sizeof(label),gWeaponCrafts[recipe].label);
    int serial=GetClientSerial(client);BeginCraftingTransaction(client,serial);
    if(!Spend(gWeaponCrafts[recipe].cost,client)){EndCraftingTransaction(client,serial);return false;}
    // No weapon entity is inspected or created here. This API has no delivery
    // result, so the settlement confirms one dispatch, not external delivery.
    FakeClientCommand(client,"%s",command);
    EndCraftingTransaction(client,serial);
    if(GetClientFromSerial(serial)==client && IsClientInGame(client))Tell(client,"已消耗材料并提交武器合成：%s。",label);
    return true;
}

bool TryCraftMaterial(int client,int building,int recipe)
{
    if(client<1 || client>MaxClients || !IsClientInGame(client))return false;
    if(!CraftingStationUsable(client,building))
    {Tell(client,"合成台已失效、未供电或没有近距离瞄准，请重新按 E。");return false;}
    if(CraftingTransactionBusy(client) || recipe<0 || recipe>=gMaterialCraftCount)return false;
    if(!HasCraftMaterials(client,gMaterialCrafts[recipe].cost))
    {Tell(client,"个人背包材料不足，未扣除材料。");return false;}
    if(!MaterialCraftHasRoom(client,recipe))
    {Tell(client,"合成后的材料将超过个人背包容量，未扣除材料。");return false;}
    int material=gMaterialCrafts[recipe].outputMaterial,amount=gMaterialCrafts[recipe].outputAmount;
    int serial=GetClientSerial(client);BeginCraftingTransaction(client,serial);
    if(!Spend(gMaterialCrafts[recipe].cost,client)){EndCraftingTransaction(client,serial);return false;}
    // Commit without Grant's truncation or intervening callbacks. Every input
    // and the full output were checked against the same personal inventory.
    gPlayerInventory[client][material]+=amount;
    EndCraftingTransaction(client,serial);
    char label[64];TranslateLabel(client,gMaterials[material].label,label,sizeof(label));
    Tell(client,"材料合成完成：%s x%d。",label,amount);return true;
}
