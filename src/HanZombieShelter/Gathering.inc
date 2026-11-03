// Fractions belong to the connected player and material, never a weapon or node.
// Inventory lifecycle owns clearing; day changes and rollback retain this state.
int gGatherRemainder[MAXPLAYERS+1][MATERIALS];

void ResetClientGathering(int client)
{
    for(int material=0;material<MATERIALS;material++)gGatherRemainder[client][material]=0;
}

int GatherCappedProduct(int left,int right,int limit)
{
    if(left<=0 || right<=0 || limit<=0)return 0;
    return left>limit/right ? limit : left*right;
}

int CalculateGatherYield(int baseYield,int multiplier,int remainder,int room,int &nextRemainder)
{
    nextRemainder=remainder;
    if(baseYield<=0 || multiplier<=0 || room<=0)return 0;
    int whole=multiplier/GATHER_MULTIPLIER_SCALE,fraction=multiplier%GATHER_MULTIPLIER_SCALE;
    // Split both factors before multiplication: resource yield may approach
    // the cell limit. Only the fractional numerator is ever multiplied raw.
    int numerator=(baseYield%GATHER_MULTIPLIER_SCALE)*fraction+remainder;
    nextRemainder=numerator%GATHER_MULTIPLIER_SCALE;
    int amount=GatherCappedProduct(baseYield,whole,room);
    amount+=GatherCappedProduct(baseYield/GATHER_MULTIPLIER_SCALE,fraction,room-amount);
    int tail=numerator/GATHER_MULTIPLIER_SCALE;
    if(tail>room-amount)tail=room-amount;
    return amount+tail;
}

bool CollectGatherMaterial(int client,int material,int multiplier,int &amount)
{
    amount=0;
    if(client<1 || client>MaxClients || material<0 || material>=gMaterialCount || multiplier<=0)return false;
    int capacity=gInventoryCap>0 ? gInventoryCap : 2000000000;
    int room=capacity-gPlayerInventory[client][material];
    if(room<=0)return false;
    int nextRemainder;
    int requested=CalculateGatherYield(gMaterials[material].yield,multiplier,
        gGatherRemainder[client][material],room,nextRemainder);
    // A positive sub-unit yield is valid work even when this hit grants zero.
    // Saturation discards integer overflow, never turning it into a debt.
    if(requested>0)
    {
        amount=Grant(material,requested,client);
        if(amount<=0)return false;
    }
    gGatherRemainder[client][material]=nextRemainder;return true;
}
