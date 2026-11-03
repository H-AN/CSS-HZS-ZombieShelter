// A crafting menu is a disposable quote tied to one exact, aimed workbench.
// Consume the quote before navigation or payment; an older End/Cancel callback
// must never invalidate a newer menu displayed synchronously from its handler.
Menu gCraftingMenuHandle[MAXPLAYERS+1];
int gCraftingMenuNonce[MAXPLAYERS+1];

enum CraftingMenuOperation
{
    CraftMenu_Root,
    CraftMenu_Weapons,
    CraftMenu_Materials,
    CraftMenu_WeaponDetails,
    CraftMenu_MaterialDetails,
    CraftMenu_CraftWeapon,
    CraftMenu_CraftMaterial,
    CraftMenu_Management
}

void ResetCraftingMenu(int client)
{
    if(client<1 || client>MaxClients)return;
    gCraftingMenuHandle[client]=null;
    gCraftingMenuNonce[client]=gCraftingMenuNonce[client]>=LEVEL_CELL_LIMIT ? 1 : gCraftingMenuNonce[client]+1;
}

void ResetAllCraftingMenus()
{
    for(int client=1;client<=MaxClients;client++)ResetCraftingMenu(client);
}

Menu NewCraftingMenu(int client,const char[] title)
{
    ResetCraftingMenu(client);
    Menu menu=new Menu(CraftingMenuHandler);
    gCraftingMenuHandle[client]=menu;menu.SetTitle("%s",title);
    return menu;
}

void AddCraftingMenuItem(Menu menu,int client,int building,CraftingMenuOperation operation,int recipe,const char[] label,bool enabled=true)
{
    char token[224];
    Format(token,sizeof(token),"%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",gCraftingMenuNonce[client],GetClientSerial(client),
        gEpoch,gConfigRevision,building,gBuildings[building].uid,gBuildings[building].generation,
        gBuildings[building].ref,gBuildings[building].level,view_as<int>(operation),recipe);
    menu.AddItem(token,label,enabled ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
}

bool ValidateCraftingMenu(int client,Menu menu,const char[] token,int &building,CraftingMenuOperation &operation,int &recipe)
{
    if(client<1 || client>MaxClients || gCraftingMenuHandle[client]!=menu)return false;
    char fields[12][24];int values[11];
    if(ExplodeString(token,":",fields,sizeof(fields),sizeof(fields[]))!=11)return false;
    for(int field=0;field<11;field++)
    {
        // EntRefs may occupy every signed 32-bit cell, including INT_MIN.
        if(field==7)
        {
            if(!ParseDefenseEntityRefToken(fields[field],values[field]))return false;
        }
        else if(!ParseLevelInteger(fields[field],values[field],true))return false;
    }
    if(values[0]!=gCraftingMenuNonce[client] || values[1]!=GetClientSerial(client)
        || values[2]!=gEpoch || values[3]!=gConfigRevision)return false;
    building=values[4];
    if(!CraftingStationInteractable(client,building) || values[5]!=gBuildings[building].uid
        || values[6]!=gBuildings[building].generation || values[7]!=gBuildings[building].ref
        || values[8]!=gBuildings[building].level
        || values[9]<view_as<int>(CraftMenu_Root) || values[9]>view_as<int>(CraftMenu_Management))return false;
    operation=view_as<CraftingMenuOperation>(values[9]);recipe=values[10];
    switch(operation)
    {
        case CraftMenu_Root,CraftMenu_Weapons,CraftMenu_Materials,CraftMenu_Management:
        {
            if(recipe!=-1)return false;
        }
        case CraftMenu_WeaponDetails,CraftMenu_CraftWeapon:
        {
            if(recipe<0 || recipe>=gWeaponCraftCount || !WeaponCraftAllowed(building,recipe))return false;
        }
        case CraftMenu_MaterialDetails,CraftMenu_CraftMaterial:
        {
            if(recipe<0 || recipe>=gMaterialCraftCount)return false;
        }
    }
    ResetCraftingMenu(client);return true;
}

void ShowCraftingMenu(int client,int building)
{
    if(!CraftingStationInteractable(client,building))return;
    char name[128],title[512];BuildingDisplayName(building,name,sizeof(name),client);
    Format(title,sizeof(title),"%s Lv.%d\n请选择合成类型\n材料均从个人背包支付%s",name,gBuildings[building].level,
        CraftingStationUsable(client,building) ? "" : "\n当前未供电，暂不能合成");
    Menu menu=NewCraftingMenu(client,title);
    AddCraftingMenuItem(menu,client,building,CraftMenu_Weapons,-1,"武器合成");
    AddCraftingMenuItem(menu,client,building,CraftMenu_Materials,-1,"材料合成");
    AddCraftingMenuItem(menu,client,building,CraftMenu_Management,-1,"合成台管理／升级");
    menu.Display(client,30);
}

void ShowWeaponCraftingMenu(int client,int building)
{
    if(!CraftingStationInteractable(client,building))return;
    char station[128],title[384];BuildingDisplayName(building,station,sizeof(station),client);
    Format(title,sizeof(title),"武器合成\n%s Lv.%d",station,gBuildings[building].level);
    Menu menu=NewCraftingMenu(client,title);
    for(int recipe=0;recipe<gWeaponCraftCount;recipe++)
    {
        char name[128],label[384];TranslateLabel(client,gWeaponCrafts[recipe].label,name,sizeof(name));
        bool allowed=WeaponCraftAllowed(building,recipe);
        if(!StrEqual(gWeaponCrafts[recipe].station,gBuildings[building].type))
            Format(label,sizeof(label),"%s（需要：%s）",name,gWeaponCrafts[recipe].station);
        else if(gBuildings[building].level<gWeaponCrafts[recipe].requiredLevel)
            Format(label,sizeof(label),"%s（需要Lv.%d，当前Lv.%d）",name,gWeaponCrafts[recipe].requiredLevel,gBuildings[building].level);
        else Format(label,sizeof(label),"%s（Lv.%d起）",name,gWeaponCrafts[recipe].requiredLevel);
        AddCraftingMenuItem(menu,client,building,CraftMenu_WeaponDetails,recipe,label,allowed);
    }
    if(gWeaponCraftCount==0)menu.AddItem("","尚未配置可用武器配方",ITEMDRAW_DISABLED);
    AddCraftingMenuItem(menu,client,building,CraftMenu_Root,-1,"返回合成台");
    menu.Display(client,30);
}

void ShowMaterialCraftingMenu(int client,int building)
{
    if(!CraftingStationInteractable(client,building))return;
    char station[128],title[384];BuildingDisplayName(building,station,sizeof(station),client);
    Format(title,sizeof(title),"材料合成\n%s Lv.%d\n所有种类与等级的合成台均可使用",station,gBuildings[building].level);
    Menu menu=NewCraftingMenu(client,title);
    for(int recipe=0;recipe<gMaterialCraftCount;recipe++)
    {
        char label[192];TranslateLabel(client,gMaterialCrafts[recipe].label,label,sizeof(label));
        AddCraftingMenuItem(menu,client,building,CraftMenu_MaterialDetails,recipe,label);
    }
    if(gMaterialCraftCount==0)menu.AddItem("","尚未配置可用材料配方",ITEMDRAW_DISABLED);
    AddCraftingMenuItem(menu,client,building,CraftMenu_Root,-1,"返回合成台");
    menu.Display(client,30);
}

void ShowWeaponCraftingDetails(int client,int building,int recipe)
{
    if(!CraftingStationInteractable(client,building) || recipe<0 || recipe>=gWeaponCraftCount || !WeaponCraftAllowed(building,recipe))return;
    char name[128],title[512];TranslateLabel(client,gWeaponCrafts[recipe].label,name,sizeof(name));
    Format(title,sizeof(title),"合成武器：%s\n需要：%s Lv.%d及以上\n从个人背包支付：",name,
        gWeaponCrafts[recipe].station,gWeaponCrafts[recipe].requiredLevel);
    bool powered=CraftingStationUsable(client,building),available=WeaponCraftCommandExists(recipe);
    bool enough=HasCraftMaterials(client,gWeaponCrafts[recipe].cost);
    Menu menu=NewCraftingMenu(client,title);
    AddCraftingMenuItem(menu,client,building,CraftMenu_CraftWeapon,recipe,!powered ? "合成台未供电，暂不能合成" :
        (!available ? "该武器暂不可用" : (!enough ? "个人材料不足" : "确认合成武器")),powered && available && enough);
    AddCraftingMenuItem(menu,client,building,CraftMenu_Weapons,-1,"返回武器合成列表");
    AddRecipeCostItems(menu,client,gWeaponCrafts[recipe].cost);
    menu.Display(client,30);
}

void ShowMaterialCraftingDetails(int client,int building,int recipe)
{
    if(!CraftingStationInteractable(client,building) || recipe<0 || recipe>=gMaterialCraftCount)return;
    int material=gMaterialCrafts[recipe].outputMaterial;
    if(material<0 || material>=gMaterialCount)return;
    char name[128],output[128],title[512];TranslateLabel(client,gMaterialCrafts[recipe].label,name,sizeof(name));
    TranslateLabel(client,gMaterials[material].label,output,sizeof(output));
    Format(title,sizeof(title),"材料合成：%s\n产出：%s x%d（持有%d）\n从个人背包支付：",name,output,
        gMaterialCrafts[recipe].outputAmount,gPlayerInventory[client][material]);
    bool powered=CraftingStationUsable(client,building),enough=HasCraftMaterials(client,gMaterialCrafts[recipe].cost);
    bool room=MaterialCraftHasRoom(client,recipe);
    Menu menu=NewCraftingMenu(client,title);
    AddCraftingMenuItem(menu,client,building,CraftMenu_CraftMaterial,recipe,!powered ? "合成台未供电，暂不能合成" :
        (!enough ? "个人材料不足" : (!room ? "产出材料的背包容量不足" : "确认合成材料")),powered && enough && room);
    AddCraftingMenuItem(menu,client,building,CraftMenu_Materials,-1,"返回材料合成列表");
    AddRecipeCostItems(menu,client,gMaterialCrafts[recipe].cost);
    menu.Display(client,30);
}

public int CraftingMenuHandler(Menu menu,MenuAction action,int client,int item)
{
    if(action==MenuAction_End)
    {
        for(int target=1;target<=MaxClients;target++)if(gCraftingMenuHandle[target]==menu)ResetCraftingMenu(target);
        delete menu;return 0;
    }
    if(client<1 || client>MaxClients || gCraftingMenuHandle[client]!=menu)return 0;
    if(action==MenuAction_Cancel){ResetCraftingMenu(client);return 0;}
    if(action!=MenuAction_Select)return 0;
    char token[224];int building,recipe;CraftingMenuOperation operation;
    if(item<0 || item>=menu.ItemCount || !menu.GetItem(item,token,sizeof(token))
        || !ValidateCraftingMenu(client,menu,token,building,operation,recipe))
    {
        if(gCraftingMenuHandle[client]==menu)ResetCraftingMenu(client);
        if(IsClientInGame(client))Tell(client,"合成菜单已过期，请靠近并瞄准合成台重新按 E。");
        return 0;
    }
    switch(operation)
    {
        case CraftMenu_Root:ShowCraftingMenu(client,building);
        case CraftMenu_Weapons:ShowWeaponCraftingMenu(client,building);
        case CraftMenu_Materials:ShowMaterialCraftingMenu(client,building);
        case CraftMenu_WeaponDetails:ShowWeaponCraftingDetails(client,building,recipe);
        case CraftMenu_MaterialDetails:ShowMaterialCraftingDetails(client,building,recipe);
        case CraftMenu_CraftWeapon:
        {
            // A successful command may open its own weapon-system menu. Leave
            // that UI intact; the player can aim at the bench and press E again.
            if(!TryCraftWeapon(client,building,recipe))ShowWeaponCraftingDetails(client,building,recipe);
        }
        case CraftMenu_CraftMaterial:
        {
            TryCraftMaterial(client,building,recipe);ShowMaterialCraftingDetails(client,building,recipe);
        }
        case CraftMenu_Management:ShowDefenseMenu(client,building);
    }
    return 0;
}
