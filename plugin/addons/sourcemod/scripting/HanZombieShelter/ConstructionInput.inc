// A claimed right-click is consumed until release, including rejected refunds
// and fuel requests. An unfinished campfire always takes the cancellation path.
// This prevents the same input from also executing a knife secondary attack.
bool gConstructionCancelHeld[MAXPLAYERS+1];

void ResetConstructionCancelInput(int client)
{
    if(client>0 && client<=MaxClients)gConstructionCancelHeld[client]=false;
}

bool HandleConstructionCancelInput(int client,int &buttons,int previousButtons)
{
    if(client<1 || client>MaxClients)return false;
    if((buttons&IN_ATTACK2)==0){gConstructionCancelHeld[client]=false;return false;}
    if(gConstructionCancelHeld[client]){buttons&=~IN_ATTACK2;return true;}
    if((previousButtons&IN_ATTACK2)!=0 || !Running() || !LiveHuman(client))return false;
    int knife;
    if(!HeldKnifeSlotWeapon(client,knife) || !ResourceMeleeHit(client,client))return false;
    int building=FindBuilding(AimedEntity(client,gUseDistance));
    if(building<0 || !gBuildings[building].used || !gBuildings[building].alive)return false;
    bool cancel=gBuildings[building].constructing;
    if(!cancel && gBuildings[building].kind!=Kind_Campfire)return false;
    gConstructionCancelHeld[client]=true;buttons&=~IN_ATTACK2;
    if(cancel)CancelUnfinishedConstruction(client,building);
    else RefuelCampfire(client,building);
    return true;
}
