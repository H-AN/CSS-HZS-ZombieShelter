#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#undef REQUIRE_PLUGIN
#include <HanZombieShelterAPI>

public Plugin myinfo={name="Shelter API consumer test",author="Codex",description="Independent optional API client",version="1.0",url=""};
int aPassed,aFailed,aPhases,aStates,aRounds,aOutcomes,aPowerChanges;
void APICheck(bool ok,const char[] text)
{
    if(ok)aPassed++;else aFailed++;
    PrintToServer("[ShelterAPI] %s %s",ok ? "PASS" : "FAIL",text);
}
bool HasShelterAPI(){return LibraryExists("HanZombieShelter") && GetFeatureStatus(FeatureType_Native,"HZSH_GetEnvironmentState")==FeatureStatus_Available;}
public void OnPluginStart(){RegServerCmd("hzsh_api_test",APICommand);}
public Action APICommand(int args)
{
    char which[32];GetCmdArg(1,which,sizeof(which));
    if(StrEqual(which,"absent"))
    {APICheck(!HasShelterAPI(),"optional consumer survives provider absence");return Plugin_Handled;}
    APICheck(HasShelterAPI(),"independent consumer resolves provider library and natives");if(!HasShelterAPI())return Plugin_Handled;
    bool enabled;HZSHPhase phase;HZSHEnvironmentPhase environment;int day,revision;
    APICheck(HZSH_GetEnvironmentState(enabled,phase,environment,day,revision),"atomic environment snapshot is ready");
    APICheck(HZSH_GetAPIVersion()==1 && HZSH_GetEnvironmentAPIVersion()==1,"public API versions are one");
    APICheck(enabled && revision>0 && day>=1,"snapshot exposes an enabled map, revision and day");
    if(StrEqual(which,"night"))APICheck(phase==HZSHPhase_Night && environment==HZSHEnvironment_Night,"night getter agrees with notification");
    if(StrEqual(which,"day"))APICheck(phase==HZSHPhase_Day && environment==HZSHEnvironment_Day,"day getter agrees with notification");
    if(StrEqual(which,"ending"))APICheck(phase==HZSHPhase_Ending && !HZSH_IsRunning(),"ending snapshot stops active gameplay");
    if(StrEqual(which,"night_ending"))APICheck(phase==HZSHPhase_Ending && environment==HZSHEnvironment_Night,"night settlement preserves the last effective night environment");
    if(HZSH_IsRunning())
    {
        APICheck(HZSH_GetPhaseTimeRemaining()>=0.0 && HZSH_GetPhaseTimeRemaining()<=180.0,"phase remaining time is bounded");
        APICheck(HZSH_GetBaseCount()==3,"consumer sees the three configured bases");
        int uid,level,hp,maxHP;bool alive;float output,demand,allocated;int offline;
        APICheck(HZSH_GetBaseInfo(0,uid,level,hp,maxHP,alive) && uid>0 && maxHP>=hp,"base info getter resolves a stable UID");
        APICheck(HZSH_GetBasePower(0,output,demand,allocated,offline) && output>=0.0 && demand>=allocated,"power snapshot is coherent");
        HZSHBuildingKind kind;int core;bool powered;float required,generation;
        APICheck(HZSH_GetBuildingInfo(uid,kind,core,level,hp,maxHP,alive,powered,required,generation) && kind==HZSHBuilding_Base && core==0,"building query uses the stable UID");
        APICheck(!HZSH_GetBasePower(-1,output,demand,allocated,offline),"invalid core is rejected without runtime error");
        APICheck(!HZSH_GetBuildingInfo(-1,kind,core,level,hp,maxHP,alive,powered,required,generation),"invalid building UID is rejected");
        int materials[5];APICheck(!HZSH_GetPlayerMaterials(0,materials),"invalid player inventory query is rejected");
    }
    PrintToServer("[ShelterAPISummary] Passed=%d Failed=%d phase=%d environment=%d day=%d revision=%d phases=%d states=%d rounds=%d outcomes=%d power_changes=%d",
        aPassed,aFailed,phase,environment,day,revision,aPhases,aStates,aRounds,aOutcomes,aPowerChanges);return Plugin_Handled;
}
public void HZSH_OnEnvironmentStateChanged(int revision){aStates++;}
public void HZSH_OnPhaseChanged(HZSHPhase previous,HZSHPhase current,int day,float duration)
{
    aPhases++;bool enabled;HZSHPhase phase;HZSHEnvironmentPhase environment;int currentDay,revision;
    if(HasShelterAPI() && HZSH_GetEnvironmentState(enabled,phase,environment,currentDay,revision))
        APICheck(phase==current && currentDay==day,"phase forward runs after committed snapshot");
}
public void HZSH_OnRoundStarted(int day,bool restored){aRounds++;}
public void HZSH_OnOutcome(bool victory,int previousDay,int nextDay,bool terminal){aOutcomes++;}
public void HZSH_OnBasePowerChanged(int core,float production,float demand,float allocated,int offline){aPowerChanges++;}
