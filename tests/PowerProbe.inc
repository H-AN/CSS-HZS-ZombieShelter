Building tPowerBuildings[MAX_BUILDINGS];
int tPowerRetainedGeneratorUID,tPowerRetainedLampUID,tPowerRetainedHP;
int tPowerRemovalRefs[2];
int tPowerUpgradeGenerator=-1,tPowerUpgradeLamp=-1,tPowerUpgradeMaterials[MATERIALS];
public Action VerifyPowerCleanup(Handle timer,any epoch)
{
    if(epoch!=gEpoch)return Plugin_Stop;
    Check(Resolve(tPowerRemovalRefs[0])<0,"expired projectile native removal completes after engine cleanup");
    Check(Resolve(tPowerRemovalRefs[1])<0,"destroyed lamp child native removal completes after engine cleanup");
    return Plugin_Stop;
}

bool PowerAimBuilding(int client,int b)
{
    if(SafetyAimBuilding(client,b))return true;
    int entity=Resolve(gBuildings[b].ref);if(entity<0)return false;
    float low[3],high[3];ModelWorldBounds(entity,gBuildings[b].origin,gBuildings[b].angles,low,high);
    for(int step=0;step<16;step++)for(int height=0;height<5;height++)
    {
        float angle=DegToRad(float(step)*22.5),position[3],target[3];position=gBuildings[b].origin;
        position[0]+=Cosine(angle)*65.0;position[1]+=Sine(angle)*65.0;
        target[0]=(low[0]+high[0])*0.5;target[1]=(low[1]+high[1])*0.5;
        target[2]=low[2]+(high[2]-low[2])*(0.1+float(height)*0.2);position[2]=target[2]-64.0;
        Aim(client,position,target);if(BuildingInUseRange(client,b))return true;
    }
    PrintToServer("[ShelterPowerFixture] failed aim b=%d body=%d bounds=%.1f %.1f %.1f / %.1f %.1f %.1f",b,entity,low[0],low[1],low[2],high[0],high[1],high[2]);return false;
}

int PowerRecipe(BuildingKind kind)
{
    for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==kind)return r;return -1;
}

void RemovePowerFixture(int b)
{
    if(b<0)return;
    gBuildings[b].alive=false;RemoveBuildingEffect(b);int ref=gBuildings[b].ref;gBuildings[b].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);
    gBuildings[b].used=false;RecalculatePower();
}

int PowerFixture(BuildingKind kind,const float origin[3],int core=0)
{
    // The Boss marker is at terrain height; stock machinery extends below its
    // origin. Keep isolated combat bodies above terrain. Paid restoration below
    // separately uses the full production floor-placement path.
    float lifted[3];lifted=origin;lifted[2]+=256.0;
    int b=TestBuild(PowerRecipe(kind),lifted);if(b<0)return -1;
    gBuildings[b].powerCore=core;gBuildings[b].gridAssigned=true;RecalculatePower();return b;
}

void TestPower(int client)
{
    Check(gPowerEnabled && gRecipeCount==9,"0.4 loads three new recipes and enables base power grids");
    for(int b=0;b<MAX_BUILDINGS;b++)tPowerBuildings[b]=gBuildings[b];
    int savedMaterials[MATERIALS];savedMaterials=gPlayerInventory[client];
    for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=100000;
    float position[3];position=gBosses[0].origin;position[0]+=550.0;position[1]+=450.0;
    int generator=PowerFixture(Kind_Generator,position);position[1]+=150.0;
    int lamp=PowerFixture(Kind_Light,position);position[1]+=150.0;
    int turret=PowerFixture(Kind_Turret,position);position[1]+=150.0;
    int flame=PowerFixture(Kind_Flame,position);position[1]+=150.0;
    int grenade=PowerFixture(Kind_Grenade,position);position[1]+=150.0;
    int amplifier=PowerFixture(Kind_Amplifier,position);position[1]+=150.0;
    int remote=PowerFixture(Kind_Turret,position,1);
    Check(generator>=0 && lamp>=0 && turret>=0 && flame>=0 && grenade>=0 && amplifier>=0 && remote>=0,"generator, lamp, powered weapons and amplifier create actual engine bodies");
    if(generator>=0 && lamp>=0 && turret>=0 && flame>=0 && grenade>=0 && amplifier>=0 && remote>=0)
    {
        Check(FloatAbs(GeneratorOutput(generator)-100.0)<0.01,"level-one full-health generator produces 100 capacity");
        Check(BuildingPowered(turret) && BuildingPowered(flame) && BuildingPowered(grenade) && BuildingPowered(lamp),"one generator supplies several complete loads in its own grid");
        Check(!BuildingPowered(remote) && gGridOffline[1]==1 && gGridProduction[1]==0.0,"base B cannot consume base A generation");
        Check(GridAt(gCores[0].origin)==0 && GridAt(gCores[1].origin)==1,"real build locations select the matching base grid");
        int light=Resolve(gBuildings[lamp].effectRef),lightRef=gBuildings[lamp].effectRef;
        Check(light>0 && GetEntProp(light,Prop_Data,"m_On")!=0,"powered lamp switches on the native light_dynamic");
        Check(FloatAbs(GetEntPropFloat(light,Prop_Send,"m_Radius")-500.0)<0.1,"lamp entity exposes the configured lighting radius");
        DamageBuilding(generator,700,true); // 30 capacity: turret10 + flame15 + amplifier5; lamp cannot fit.
        Check(FloatAbs(GeneratorOutput(generator)-30.0)<0.01,"NPC-style logical damage reduces generation in exact HP proportion");
        Check(BuildingPowered(turret) && BuildingPowered(flame) && !BuildingPowered(grenade) && !BuildingPowered(lamp),"shortage stops whole loads according to priority and stable order");
        Check(GetEntProp(light,Prop_Data,"m_On")==0,"shortage turns off actual native light illumination");
        int shots=GrenadeCount();gBuildings[grenade].nextFire=0.0;GrenadeFire(grenade,GetGameTime());
        Check(GrenadeCount()==shots,"unpowered grenade turret cannot launch");
        float quote;char profile[128];BuildingProfilePath(turret,profile,sizeof(profile));
        Check(ReadLevelFloat(C_Turrets,profile,2,"power_required",0.0,quote) && quote==15.0,"ordinary turret upgrade raises power load from 10 to 15");
        // Real CS:S knife flags pass through the production repair hook.
        GivePlayerItem(client,"weapon_knife");int knife=-1;
        for(int slot=0;slot<6;slot++){int weapon=GetPlayerWeaponSlot(client,slot);if(weapon>0){char cls[64];GetEntityClassname(weapon,cls,sizeof(cls));if(StrEqual(cls,"weapon_knife")){knife=weapon;break;}}}
        if(knife>0)SetEntPropEnt(client,Prop_Send,"m_hActiveWeapon",knife);
        gNextRepair[client]=0.0;int before=gBuildings[generator].hp;
        SDKHooks_TakeDamage(Resolve(gBuildings[generator].ref),client,client,1.0,DMG_BULLET|DMG_NEVERGIB,_,_,_,false);
        Check(gBuildings[generator].hp==before+50 && FloatAbs(GeneratorOutput(generator)-35.0)<0.01,"actual knife damage hook repairs generator HP and restores capacity");
        Check(BuildingPowered(lamp) && GetEntProp(light,Prop_Data,"m_On")!=0,"repair crosses the lamp load threshold and automatically relights it");
        before=gBuildings[generator].hp;SDKHooks_TakeDamage(Resolve(gBuildings[generator].ref),client,client,300.0,DMG_BLAST,_,_,_,false);
        Check(gBuildings[generator].hp==before,"friendly explosive damage does not reduce logical generator HP");
        gBuildings[generator].hp=gBuildings[generator].maxHP;RecalculatePower();
        for(int i=0;i<20;i++)PowerTick();Check(gBuildings[lamp].effectRef==lightRef,"repeated power ticks retain one lamp effect entity");
        int n=-1;for(int i=0;i<MAX_NPCS;i++)if(DefenseTarget(i) && gNPCs[i].boss<0){n=i;break;}
        Check(n>=0,"grenade scenario has an actual ordinary NPC target");
        if(n>=0)
        {
            int entity=Resolve(gNPCs[n].ref),savedHP=GetEntProp(entity,Prop_Data,"m_iHealth");float savedPosition[3];GetEntPropVector(entity,Prop_Data,"m_vecOrigin",savedPosition);
            float target[3],muzzle[3];target=gBuildings[grenade].origin;target[0]+=180.0;TeleportEntity(entity,target,NULL_VECTOR,NULL_VECTOR);SetEntProp(entity,Prop_Data,"m_iHealth",5000);
            EntityCenter(entity,target);muzzle=gBuildings[grenade].origin;muzzle[2]+=64.0;
            int humanHP=GetClientHealth(client),expected=RoundToNearest(gBuildings[grenade].damage);ApplyProgressionDamage(n,client,expected,true);
            gBuildings[grenade].nextFire=0.0;GrenadeFire(grenade,GetGameTime());
            Check(GrenadeCount()==1,"powered automatic grenade turret launches a visible projectile entity");
            int p=-1;for(int i=0;i<MAX_GRENADES;i++)if(gGrenades[i].used){p=i;break;}
            if(p>=0)
            {
                float start=gGrenades[p].lastTick;float oldPos[3];oldPos=gGrenades[p].position;
                StepGrenade(p,start+0.05);Check(!gGrenades[p].used || GetVectorDistance(oldPos,gGrenades[p].position)>1.0,"projectile travels before impact rather than applying instant target damage");
                for(int i=2;i<100 && gGrenades[p].used;i++)StepGrenade(p,start+float(i)*0.05);
                Check(!gGrenades[p].used && GetEntProp(entity,Prop_Data,"m_iHealth")==5000-expected,"swept projectile impact applies exactly one progressed HZS blast hit");
                int hp=GetEntProp(entity,Prop_Data,"m_iHealth");ExplodeGrenade(p,target);Check(GetEntProp(entity,Prop_Data,"m_iHealth")==hp,"replayed explosion cannot apply a second hit");
            }
            Check(GetClientHealth(client)==humanHP,"grenade blast never applies native damage to teammates");
            ClearGrenades();int oldLimit=gGrenadeLimit;gGrenadeLimit=1;
            Check(LaunchGrenade(grenade,muzzle,target) && !LaunchGrenade(grenade,muzzle,target) && GrenadeCount()==1,"in-flight limit rejects extra projectiles without extra entities");
            for(int i=0;i<MAX_GRENADES;i++)if(gGrenades[i].used){tPowerRemovalRefs[0]=gGrenades[i].ref;StepGrenade(i,gGrenades[i].expires+0.1);Check(!gGrenades[i].used && gGrenades[i].ref==INVALID_ENT_REFERENCE,"expired projectile commits cleanup without late blast");}
            gGrenadeLimit=oldLimit;
            int oldCeiling=gEntityConstructionCeiling;gEntityConstructionCeiling=GetEntityCount();
            Check(!LaunchGrenade(grenade,muzzle,target) && GrenadeCount()==0,"projectile respects the full-server construction entity budget");gEntityConstructionCeiling=oldCeiling;
            Check(LaunchGrenade(grenade,muzzle,target),"projectile creation resumes when entity capacity returns");
            for(int i=0;i<MAX_GRENADES;i++)if(gGrenades[i].used){gGrenades[i].epoch--;StepGrenade(i,GetGameTime());}
            Check(GrenadeCount()==0,"expired round epoch prevents cross-round projectile damage");
            SetEntProp(entity,Prop_Data,"m_iHealth",savedHP);gNPCs[n].lastHP=savedHP;TeleportEntity(entity,savedPosition,NULL_VECTOR,NULL_VECTOR);
        }
        DamageBuilding(generator,gBuildings[generator].hp,true);
        Check(gGridProduction[0]==0.0 && !BuildingPowered(turret) && !BuildingPowered(lamp),"destroying the generator immediately stops dependent facilities");
        tPowerRemovalRefs[1]=gBuildings[lamp].effectRef;DamageBuilding(lamp,gBuildings[lamp].hp,true);
        Check(gBuildings[lamp].effectRef==INVALID_ENT_REFERENCE,"destroying a lamp invalidates its owned light reference");
        CreateTimer(0.2,VerifyPowerCleanup,gEpoch,TIMER_FLAG_NO_MAPCHANGE);
    }
    for(int b=gCoreCount;b<MAX_BUILDINGS;b++)
    {
        if(gBuildings[b].uid!=tPowerBuildings[b].uid)RemovePowerFixture(b);
        gBuildings[b]=tPowerBuildings[b];
    }
    gPlayerInventory[client]=savedMaterials;ClearGrenades();RecalculatePower();AtBase(client);
}

void TestPowerRetention(int client)
{
    AtBase(client);
    int inventory[MATERIALS];inventory=gPlayerInventory[client];for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=10000;
    int generator=-1,lamp=-1,recipe=PowerRecipe(Kind_Generator);
    if(SafetyFindConstructionAim(client,recipe) && BuildRecipe(client,recipe))
        for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].uid==gUID)generator=b;
    recipe=PowerRecipe(Kind_Light);
    if(SafetyFindConstructionAim(client,recipe) && BuildRecipe(client,recipe))
        for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].uid==gUID)lamp=b;
    gPlayerInventory[client]=inventory;
    Check(generator>=0 && lamp>=0,"real-round power retention fixture creates generator and lamp");if(generator<0 || lamp<0)return;
    gBuildings[lamp].powerCore=gBuildings[generator].powerCore;AtBase(client);
    gBuildings[generator].hp=700;RecalculatePower();tPowerRetainedHP=700;
    tPowerRetainedGeneratorUID=gBuildings[generator].uid;tPowerRetainedLampUID=gBuildings[lamp].uid;
    StartNight();QueueOutcome(false);
}

void TestPowerUpgradePrepare(int client)
{
    tPowerUpgradeMaterials=gPlayerInventory[client];for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=10000;
    tPowerUpgradeGenerator=-1;tPowerUpgradeLamp=-1;
    int recipe=PowerRecipe(Kind_Generator);
    if(SafetyFindConstructionAim(client,recipe) && BuildRecipe(client,recipe))
        for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].uid==gUID)tPowerUpgradeGenerator=b;
    recipe=PowerRecipe(Kind_Light);
    if(SafetyFindConstructionAim(client,recipe) && BuildRecipe(client,recipe))
        for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].uid==gUID)tPowerUpgradeLamp=b;
    Check(tPowerUpgradeGenerator>=0 && tPowerUpgradeLamp>=0,"paid upgrade fixtures use production ground placement and construction");
}

void TestPowerUpgradeVerify(int client)
{
    int generator=tPowerUpgradeGenerator,lamp=tPowerUpgradeLamp;
    if(generator>=0 && lamp>=0)
    {
        Check(PowerAimBuilding(client,generator),"player aims at the generator collider after native physics initialization");
        Check(TryUpgrade(client,generator) && GeneratorOutput(generator)==200.0,"paid level-two upgrade refreshes generator capacity immediately");
        Check(TryUpgrade(client,generator) && GeneratorOutput(generator)==350.0,"paid level-three upgrade reaches full-health 350 capacity");
        Check(PowerAimBuilding(client,lamp),"player aims at the lamp collider after native physics initialization");
        int effect=Resolve(gBuildings[lamp].effectRef);
        Check(TryUpgrade(client,lamp) && effect>0 && GetEntPropFloat(effect,Prop_Send,"m_Radius")==700.0,"paid lamp upgrade refreshes native radius on the same child entity");
    }
    RemovePowerFixture(lamp);RemovePowerFixture(generator);gPlayerInventory[client]=tPowerUpgradeMaterials;AtBase(client);
}

void TestPowerRestored(int client)
{
    int generator=-1,lamp=-1;
    for(int b=0;b<MAX_BUILDINGS;b++)
    {if(gBuildings[b].used && gBuildings[b].uid==tPowerRetainedGeneratorUID)generator=b;if(gBuildings[b].used && gBuildings[b].uid==tPowerRetainedLampUID)lamp=b;}
    Check(generator>=0 && lamp>=0 && Running(),"real native round rebuild retains generator and lamp UIDs");
    if(generator>=0 && lamp>=0)
    {
        Check(gBuildings[generator].hp==tPowerRetainedHP && GeneratorOutput(generator)==70.0,"failure rebuild retains generator damage and proportional output");
        int effect=Resolve(gBuildings[lamp].effectRef);
        Check(BuildingPowered(lamp) && effect>0 && GetEntProp(effect,Prop_Data,"m_On")!=0,"failure rebuild recreates and powers the owned light");
        RemovePowerFixture(lamp);RemovePowerFixture(generator);
    }
    AtBase(client);
}
