// Map-specific repairs are explicit identities and coordinates, never a broad
// no-collision rule for ordinary map structures or player-built defenses.
int gMapRemovedVines,gMapMovedSpawns;

void ApplyMapAdjustments()
{
    gMapRemovedVines=0;gMapMovedSpawns=0;
    if(Node(C_Map,"MapAdjustments/RemoveDecorativeBrushes") && gCfg[C_Map].GotoFirstSubKey())
    {
        do
        {
            char model[64];float expected[3];gCfg[C_Map].GetString("model",model,sizeof(model));gCfg[C_Map].GetVector("origin",expected);
            if(model[0]!='*')continue;
            int entity=-1;
            while((entity=FindEntityByClassname(entity,"func_brush"))!=-1)
            {
                char actual[64];float origin[3];GetEntPropString(entity,Prop_Data,"m_ModelName",actual,sizeof(actual));
                GetEntPropVector(entity,Prop_Data,"m_vecOrigin",origin);
                if(StrEqual(actual,model) && GetVectorDistance(origin,expected)<1.0)
                {RemoveEntity(entity);gMapRemovedVines++;break;}
            }
        }while(gCfg[C_Map].GotoNextKey());
    }
    if(Node(C_Map,"MapAdjustments/NPCSpawnOverrides") && gCfg[C_Map].GotoFirstSubKey())
    {
        do
        {
            char name[64];float position[3],angles[3];gCfg[C_Map].GetSectionName(name,sizeof(name));
            gCfg[C_Map].GetVector("origin",position);gCfg[C_Map].GetVector("angles",angles);
            bool valid=true;for(int a=0;a<3;a++)if(!(position[a]>=-15000.0 && position[a]<=15000.0))valid=false;
            if(!valid || TR_PointOutsideWorld(position))continue;
            int entity=-1;
            while((entity=FindEntityByClassname(entity,"info_player_terrorist"))!=-1)
            {
                char actual[64];GetEntPropString(entity,Prop_Data,"m_iName",actual,sizeof(actual));if(!StrEqual(name,actual))continue;
                float mins[3]={-24.0,-24.0,0.0},maxs[3]={24.0,24.0,84.0};
                if(ClearHull(position,mins,maxs,entity))
                {TeleportEntity(entity,position,angles,NULL_VECTOR);gMapMovedSpawns++;}
                else LogMessage("[ShelterMap] spawn override %s obstructed; original point retained",name);
                break;
            }
        }while(gCfg[C_Map].GotoNextKey());
    }
    LogMessage("[ShelterMap] explicit repairs removed_vine_brushes=%d moved_npc_spawns=%d",gMapRemovedVines,gMapMovedSpawns);
}
