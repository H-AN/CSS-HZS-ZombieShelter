// Configuration-only checks; no client, NPC, fake client, or building is created.
void TestV05VariantLogic()
{
    TestV05SurfacePlacementAngles();
    KeyValues original=gCfg[C_Turrets];int originalCount=gRecipeCount;
    KeyValues fixture=new KeyValues("ShelterTurrets");gCfg[C_Turrets]=fixture;
    fixture.SetNum("schema_version",2);fixture.JumpToKey("Types",true);fixture.JumpToKey("probe_variant",true);
    fixture.SetString("model","models/zshelter/shelter_b_turret01.mdl");fixture.SetString("display_name","自定义炮塔");
    fixture.SetString("description","Literal configurable description");fixture.SetString("render_rgb","12 34 56");fixture.SetNum("render_alpha",200);fixture.SetNum("skin",0);
    fixture.JumpToKey("Placement",true);fixture.SetString("mode","surface");fixture.SetString("origin_offset","10 20 30");
    fixture.SetString("angle_offset","0 15 0");fixture.SetFloat("max_distance",768.0);fixture.SetNum("allow_overlap",1);fixture.GoBack();
    fixture.JumpToKey("Attachments",true);fixture.SetString("muzzle_offset","4 2 48");fixture.GoBack();
    fixture.JumpToKey("Levels",true);fixture.JumpToKey("1",true);fixture.SetNum("max_hp",100);fixture.SetFloat("damage",10.0);
    fixture.SetFloat("fire_interval",0.05);fixture.SetString("build_half_extents","100 80 0");
    fixture.JumpToKey("upgrade_cost",true);fixture.SetNum("wood",3);fixture.GoBack();fixture.GoBack();
    fixture.JumpToKey("10",true);fixture.JumpToKey("upgrade_cost",true);fixture.SetNum(gMaterials[gMaterialCount-1].id,1);fixture.GoBack();fixture.GoBack();fixture.GoBack();
    fixture.JumpToKey("Growth",true);fixture.SetNum("max_level",10);fixture.SetNum("anchor_level",1);
    fixture.JumpToKey("Stats",true);fixture.SetFloat("damage_per_level",2.0);fixture.SetFloat("build_half_extents_x_per_level",10.0);
    fixture.GoBack();fixture.JumpToKey("UpgradeCost",true);fixture.SetNum("wood_per_level",3);
    fixture.Rewind();fixture.JumpToKey("BuildRecipes",true);
    for(int r=0;r<36;r++)
    {
        char id[64];Format(id,sizeof(id),"probe_recipe_%d",r);fixture.JumpToKey(id,true);
        fixture.SetString("output_type","probe_variant");fixture.SetString("display_name","自定义炮塔");
        fixture.JumpToKey("cost",true);fixture.SetNum("wood",1);fixture.GoBack();fixture.GoBack();
    }
    Check(ValidateLevelProfile(C_Turrets,"Types/probe_variant"),"variant Growth profile resolves ten levels without preallocating levels");
    float interval,damage,half[3];ReadLevelFloat(C_Turrets,"Types/probe_variant",1,"fire_interval",0.5,interval);
    ReadLevelFloat(C_Turrets,"Types/probe_variant",10,"damage",0.0,damage);
    Check(FloatAbs(interval-0.1)<0.001 && FloatAbs(damage-28.0)<0.001,"positive sub-0.1 interval normalizes while uncovered Lv10 damage grows from anchor");
    Check(ReadBuildHalfExtents(C_Turrets,"Types/probe_variant",3,half) && half[0]==120.0 && half[1]==80.0,
        "build range vector alias supplies anchor defaults for scalar per-level growth");
    Node(C_Turrets,"Types/probe_variant/Levels/1");fixture.SetString("build_half_extents","100 80 1");
    Check(!ReadBuildHalfExtents(C_Turrets,"Types/probe_variant",1,half),"build rectangle vector rejects nonzero Z instead of silently discarding a height field");
    Node(C_Turrets,"Types/probe_variant/Levels/1");fixture.SetString("build_half_extents","100 80 0");
    int cost[MATERIALS];
    Check(ReadLevelUpgradeCost(C_Turrets,"Types/probe_variant",10,cost) && cost[gMaterialCount-1]==1 && cost[0]==0,
        "explicit high-level special-material cost replaces the entire formula cost");
    Check(ReadRecipes() && gRecipeCount>=36,"recipe catalogue loads more than the previous global 32-variant ceiling");
    int recipe=FindRecipeByID(C_Turrets,"probe_recipe_35");
    Check(recipe>=0 && StrEqual(gRecipes[recipe].label,"自定义炮塔") && gRecipes[recipe].meta.render[0]==12
        && gRecipes[recipe].meta.render[3]==200 && gRecipes[recipe].meta.skin==0,
        "literal custom names and per-variant RGBA/skin are loaded without adding translation phrases");
    Check(recipe>=0 && gRecipes[recipe].meta.maxDistance==768.0 && gRecipes[recipe].meta.originOffset[1]==20.0
        && gRecipes[recipe].meta.muzzleOffset[0]==4.0 && StrEqual(gRecipes[recipe].meta.placementMode,"surface"),
        "placement and muzzle metadata remain attached to the selected stable recipe");
    Node(C_Turrets,"Types/probe_variant");fixture.SetNum("skin",2000000);BuildingMetadata skinMeta;
    Check(ReadTypeMetadata(C_Turrets,"probe_variant",skinMeta) && skinMeta.skin>=0 && skinMeta.skin<=255,
        "unbounded configured skin is clamped and never reaches the entity as a huge cell");
    int skinCount=BuildingModelSkinCount("models/zshelter/shelter_b_turret01.mdl");
    Check(skinCount==0 || skinMeta.skin<skinCount,"mounted model header constrains skin to actual available families when readable");
    Node(C_Turrets,"Types/probe_variant/Levels/1");fixture.SetFloat("fire_interval",0.0);
    Check(!ValidateLevelProfile(C_Turrets,"Types/probe_variant"),"zero fire interval disables its type rather than becoming unlimited fire");
    Node(C_Turrets,"Types/probe_variant/Levels/1");fixture.SetFloat("fire_interval",0.5);
    Node(C_Turrets,"Types/probe_variant/Levels/10/upgrade_cost");fixture.SetNum("unregistered_probe_core",1);
    Check(!ReadLevelUpgradeCost(C_Turrets,"Types/probe_variant",10,cost),"unknown upgrade material cannot silently become a free high-level upgrade");
    Node(C_Turrets,"BuildRecipes/probe_recipe_0/cost");fixture.SetNum("unregistered_probe_core",1);
    Check(!ValidateMaterialCostKeys(C_Turrets,"BuildRecipes/probe_recipe_0/cost"),"unknown construction cost is rejected before its recipe can be published");
    gCfg[C_Turrets]=original;delete fixture;
    Check(ReadRecipes() && gRecipeCount==originalCount,"variant probe restores the original immutable configuration catalogue");
}

void TestV05SurfacePlacementAngles()
{
    float normals[4][3]={{0.0,1.0,0.0},{0.0,-1.0,0.0},{0.4,0.5,0.76811457},{0.0,0.0,-1.0}};
    float yaw[4]={90.0,270.0,37.0,18.0};
    for(int i=0;i<4;i++)
    {
        float frame[3],forwardVector[3],right[3],up[3];SurfacePlacementAngles(yaw[i],normals[i],frame);GetAngleVectors(frame,forwardVector,right,up);
        char name[192];Format(name,sizeof(name),"surface basis %d aligns to +/-Y wall, ordinary slope or negative-Z ceiling without a zero-vector fallback",i+1);
        Check(FloatAbs(GetVectorLength(forwardVector)-1.0)<0.001 && FloatAbs(GetVectorLength(up)-1.0)<0.001
            && FloatAbs(GetVectorDotProduct(forwardVector,normals[i]))<0.001 && GetVectorDotProduct(up,normals[i])>0.999
            && FloatAbs(GetVectorDotProduct(right,up))<0.001,name);
    }
}
