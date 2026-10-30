// Diagnostic build only. Fixed buffers; no per-hit disk writes or production timers.
#define V083_METRICS 8
#define V083_SAMPLES 512
static const char gV083MetricNames[][]={"WorldTick","DefenseTimer","GrenadeTimer","RecalculatePower","PowerTick","ResourceDamageHook","BuildingDamageHook","GetProgressionStats"};
float gV083Times[V083_METRICS][V083_SAMPLES],gV083Total[V083_METRICS],gV083Maximum[V083_METRICS];
int gV083Calls[V083_METRICS];
Building gV083GeneratorSaved;
bool gV083GeneratorWork;
int gV083GeneratorWorkUID;
ResourceNode gV083ResourceSaved;
int gV083Resource=-1;
int gV083NodeInventory[MATERIALS];

void V083Duration(int metric,float seconds)
{
    int at=gV083Calls[metric]++;
    gV083Total[metric]+=seconds;if(seconds>gV083Maximum[metric])gV083Maximum[metric]=seconds;
    gV083Times[metric][at%V083_SAMPLES]=seconds;
}

void V083ResetMetrics()
{
    for(int i=0;i<V083_METRICS;i++){gV083Calls[i]=0;gV083Total[i]=0.0;gV083Maximum[i]=0.0;}
}

public Action V083PerfCommand(int args)
{
    char action[32];if(args>0)GetCmdArg(1,action,sizeof(action));
    if(StrEqual(action,"reset")){V083ResetMetrics();PrintToServer("[V083Perf] aggregate counters reset");return Plugin_Handled;}
    for(int i=0;i<V083_METRICS;i++)
    {
        int samples=gV083Calls[i]<V083_SAMPLES ? gV083Calls[i]:V083_SAMPLES;float values[V083_SAMPLES];
        for(int n=0;n<samples;n++)values[n]=gV083Times[i][n];if(samples>0)SortFloats(values,samples,Sort_Ascending);
        int p95=samples>0 ? (samples-1)*95/100:0,p99=samples>0 ? (samples-1)*99/100:0;
        PrintToServer("[V083Perf] function=%s calls=%d total_ms=%.6f avg_ms=%.6f max_ms=%.6f last_samples=%d p95_ms=%.6f p99_ms=%.6f inclusive=1",
            gV083MetricNames[i],gV083Calls[i],gV083Total[i]*1000.0,gV083Calls[i]>0 ? gV083Total[i]*1000.0/float(gV083Calls[i]):0.0,
            gV083Maximum[i]*1000.0,samples,values[p95]*1000.0,values[p99]*1000.0);
    }
    PrintToServer("[V083Perf] fixed-buffer diagnostic overhead included; nested inclusive durations MUST NOT be added; no HZS profiling");return Plugin_Handled;
}

bool V083CloseAim(int actor,int body,const float origin[3])
{
    float old[3],oldAngles[3],oldVelocity[3],playerMins[3]={-16.0,-16.0,0.0},playerMaxs[3]={16.0,16.0,72.0};
    GetClientAbsOrigin(actor,old);GetClientEyeAngles(actor,oldAngles);GetEntPropVector(actor,Prop_Data,"m_vecAbsVelocity",oldVelocity);
    for(int ring=0;ring<7;ring++)for(int direction=0;direction<16;direction++)
    {
        float feet[3],start[3],end[3],angle=float(direction)*6.2831853/16.0;
        start=origin;start[0]+=Cosine(angle)*(40.0+float(ring)*12.0);start[1]+=Sine(angle)*(40.0+float(ring)*12.0);
        start[2]+=128.0;end=start;end[2]-=512.0;
        if(!V083WorkTraceFloor(actor,start,end,feet))continue;feet[2]+=0.5;
        if(!ClearHull(feet,playerMins,playerMaxs,actor,MASK_PLAYERSOLID))continue;
        for(int height=0;height<10;height++)
        {
            float target[3],eye[3],point[3];target=origin;target[2]+=8.0+float(height)*10.0;V083WorkAimAt(actor,feet,target);GetClientEyePosition(actor,eye);
            Handle trace=TR_TraceRayFilterEx(eye,target,MASK_SOLID,RayType_EndPoint,FilterExcept,actor);
            bool hit=TR_DidHit(trace) && TR_GetEntityIndex(trace)==body;TR_GetEndPosition(point,trace);delete trace;
            if(hit && GetVectorDistance(eye,point)<=50.0)
            {SetEntityMoveType(actor,MOVETYPE_NONE);PrintToServer("[V083Probe] close_aim entity=%d eye_distance=%.3f; no damage or repair invoked",body,GetVectorDistance(eye,point));return true;}
        }
    }
    TeleportEntity(actor,old,oldAngles,oldVelocity);return false;
}

void V083CheckLiveIndices()
{
    int checks,failures;
    for(int b=0;b<MAX_BUILDINGS;b++)if(gBuildings[b].used)
    {
        checks++;if(RuntimeFindBuildingUID(gBuildings[b].uid)!=b)failures++;
        int entity=Resolve(gBuildings[b].ref);if(entity>0){checks++;if(FindBuilding(entity)!=b)failures++;}
    }
    for(int n=0;n<gNodeCount;n++)if(gNodes[n].used)
    {int entity=Resolve(gNodes[n].ref);if(entity>0){checks++;if(FindNode(entity)!=n)failures++;}}
    for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used)
    {int entity=Resolve(gNPCs[n].ref);if(entity>0){checks++;if(FindNPC(entity)!=n)failures++;}}
    EnsureRuntimeBuildings();PrintToServer("[V083Probe] live_indices checks=%d failures=%d used=%d alive=%d",checks,failures,gRuntimeUsedCount,gRuntimeAliveCount);
}

void V083ResourceStatus(int actor)
{
    if(gV083Resource<0)return;int n=gV083Resource,entity=Resolve(gNodes[n].ref);
    PrintToServer("[V083Probe] resource node=%d material=%d remaining=%d growing=%d elapsed=%.3f entity=%d non_solid=%d group=%d inventory=%d",
        n,gNodes[n].material,gNodes[n].remaining,gNodes[n].growing,gNodes[n].elapsed,entity,
        entity>0 ? view_as<int>((GetEntProp(entity,Prop_Send,"m_usSolidFlags") & 4)!=0):-1,
        entity>0 ? GetEntProp(entity,Prop_Send,"m_CollisionGroup"):-1,gPlayerInventory[actor][gNodes[n].material]);
}

public Action V083ProbeCommand(int args)
{
    int actor=V083WorkHuman();char action[32];GetCmdArg(1,action,sizeof(action));
    if(StrEqual(action,"indices")){V083CheckLiveIndices();return Plugin_Handled;}
    if(!V083WorkSession(actor)){PrintToServer("[V083Probe] requires the original one-human work session");return Plugin_Handled;}
    if(StrEqual(action,"generator_prepare"))
    {
        char mode[32];GetCmdArg(2,mode,sizeof(mode));int b=tV083WorkPowerGenerator;
        if(!V083WorkPowerGeneratorValid() || gV083GeneratorWork || (!StrEqual(mode,"repair") && !StrEqual(mode,"construction")))return Plugin_Handled;
        if(!V083CloseAim(actor,Resolve(gBuildings[b].ref),gBuildings[b].origin)){PrintToServer("[V083Probe] no real close aim");return Plugin_Handled;}
        gV083GeneratorSaved=gBuildings[b];gV083GeneratorWorkUID=gBuildings[b].uid;gV083GeneratorWork=true;
        gBuildings[b].hp=1;gBuildings[b].constructing=StrEqual(mode,"construction");gBuildings[b].constructionTarget=gBuildings[b].maxHP;
        gNextRepair[actor]=0.0;SyncConstructionVisual(b);MarkBuildAreasDirty();RecalculatePower();
        PrintToServer("[V083Probe] generator prepared mode=%s uid=%d hp=%d max_hp=%d constructing=%d; no attack invoked",mode,gBuildings[b].uid,gBuildings[b].hp,gBuildings[b].maxHP,gBuildings[b].constructing);
    }
    else if(StrEqual(action,"generator_status"))
    {
        int b=tV083WorkPowerGenerator;if(!V083WorkPowerGeneratorValid())return Plugin_Handled;
        PrintToServer("[V083Probe] generator uid=%d hp=%d max_hp=%d constructing=%d production=%.6f core=%d",gBuildings[b].uid,gBuildings[b].hp,gBuildings[b].maxHP,gBuildings[b].constructing,GeneratorOutput(b),gBuildings[b].powerCore);
    }
    else if(StrEqual(action,"generator_restore"))
    {
        int b=tV083WorkPowerGenerator;if(!gV083GeneratorWork || !V083WorkPowerGeneratorValid() || gBuildings[b].uid!=gV083GeneratorWorkUID)return Plugin_Handled;
        gBuildings[b]=gV083GeneratorSaved;gV083GeneratorWork=false;SyncConstructionVisual(b);MarkBuildAreasDirty();RecalculatePower();PrintToServer("[V083Probe] exact generator state restored");
    }
    else if(StrEqual(action,"resource_prepare"))
    {
        if(gV083Resource>=0)return Plugin_Handled;
        for(int n=0;n<gNodeCount;n++)if(!gNodes[n].growing && Resolve(gNodes[n].ref)>0 && V083CloseAim(actor,Resolve(gNodes[n].ref),gNodes[n].origin))
        {
            gV083Resource=n;gV083ResourceSaved=gNodes[n];gV083NodeInventory=gPlayerInventory[actor];gNextGather[actor]=0.0;
            PrintToServer("[V083Probe] resource prepared; unchanged node, real melee required");V083ResourceStatus(actor);break;
        }
    }
    else if(StrEqual(action,"resource_status"))V083ResourceStatus(actor);
    else if(StrEqual(action,"resource_deplete"))
    {
        if(gV083Resource<0)return Plugin_Handled;gNodes[gV083Resource].remaining=1;
        PrintToServer("[V083Probe] diagnostic remaining=1 only; actual client must supply final hit");
    }
    else if(StrEqual(action,"resource_restore"))
    {
        if(gV083Resource<0 || gNodes[gV083Resource].ref!=gV083ResourceSaved.ref)return Plugin_Handled;
        // Record real harvesting deltas before the legacy fixture reverses only test activity.
        for(int m=0;m<gMaterialCount;m++)tV083WorkDelta[m]+=gPlayerInventory[actor][m]-gV083NodeInventory[m];
        gNodes[gV083Resource]=gV083ResourceSaved;ApplyResourceModel(gV083Resource,Resolve(gV083ResourceSaved.ref));UpdateResourceVisual(gV083Resource,true);
        gV083Resource=-1;PrintToServer("[V083Probe] exact resource state restored, real harvest delta recorded");
    }
    return Plugin_Handled;
}

void RegisterV083Probe()
{
    RegisterV083WorkCraftingProbe();
    RegServerCmd("sm_v083_perf",V083PerfCommand);RegServerCmd("sm_v083_probe",V083ProbeCommand);
}
