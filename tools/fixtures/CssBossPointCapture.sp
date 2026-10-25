#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>

public Plugin myinfo={name="CSS temporary Boss point capture",author="Local test tooling",
    description="Record an administrator's grounded position and yaw into five per-map points",version="0.1"};

int gCaptureSequence;

bool CapturePaths(char[] map,int mapSize,char[] folder,int folderSize,char[] path,int pathSize)
{
    GetCurrentMap(map,mapSize);
    for(int i=0;map[i];i++)
        if(!((map[i]>='a' && map[i]<='z') || (map[i]>='A' && map[i]<='Z')
            || (map[i]>='0' && map[i]<='9') || map[i]=='_' || map[i]=='-'))return false;
    BuildPath(Path_SM,folder,folderSize,"data/zsh_boss_points");
    Format(path,pathSize,"%s/%s.cfg",folder,map);
    return map[0]!=0;
}

KeyValues ReadCapturedPoints(const char[] path)
{
    KeyValues kv=new KeyValues("BossSpawnPoints");
    if(FileExists(path))
    {
        if(!kv.ImportFromFile(path)){delete kv;return null;}
        char root[64];kv.GetSectionName(root,sizeof(root));
        if(!StrEqual(root,"BossSpawnPoints")){delete kv;return null;}
    }
    return kv;
}

int CapturedPointCount(KeyValues kv)
{
    int count;
    for(int i=1;i<=5;i++)
    {
        kv.Rewind();char point[16];Format(point,sizeof(point),"boss%d",i);
        if(kv.JumpToKey(point))count++;
    }
    kv.Rewind();return count;
}

bool CommitCapturedPoints(KeyValues kv,const char[] path)
{
    // Unique files preserve every previous collection. Never delete a draft.
    char pending[1024],backup[1024];int timestamp=GetTime(),sequence;
    do
    {
        sequence=++gCaptureSequence;
        Format(pending,sizeof(pending),"%s.pending.%d.%d",path,timestamp,sequence);
        Format(backup,sizeof(backup),"%s.backup.%d.%d",path,timestamp,sequence);
    }while(FileExists(pending) || FileExists(backup));
    kv.Rewind();if(!kv.ExportToFile(pending))return false;
    bool existing=FileExists(path);
    if(existing && !RenameFile(backup,path))return false;
    if(RenameFile(path,pending))return true;
    if(existing && !RenameFile(path,backup))LogError("[BossPoints] Could not restore draft; previous data remains in %s",backup);
    return false;
}

public Action CaptureBossPoint(int client,int args)
{
    char map[128],folder[1024],path[1024];
    if(!CapturePaths(map,sizeof(map),folder,sizeof(folder),path,sizeof(path)))
    {ReplyToCommand(client,"[Boss采点] 地图名称无法用于记录文件。");return Plugin_Handled;}
    KeyValues kv=ReadCapturedPoints(path);
    if(kv==null){ReplyToCommand(client,"[Boss采点] 已有记录无法读取，已保留原文件，请先检查。");return Plugin_Handled;}
    char argument[16];if(args>0)GetCmdArg(1,argument,sizeof(argument));
    if(args==0 || (args==1 && StrEqual(argument,"list",false)))
    {
        ReplyToCommand(client,"[Boss采点] 地图 %s，已记录 %d/5。用 !bosspoint 1 至 !bosspoint 5 记录；同编号可更新。",map,CapturedPointCount(kv));
        for(int i=1;i<=5;i++)
        {
            kv.Rewind();char point[16];Format(point,sizeof(point),"boss%d",i);
            if(!kv.JumpToKey(point))continue;
            char origin[128],angles[128];kv.GetString("origin",origin,sizeof(origin));kv.GetString("angles",angles,sizeof(angles));
            ReplyToCommand(client,"[Boss采点] %s: origin=%s; angles=%s",point,origin,angles);
        }
        ReplyToCommand(client,"[Boss采点] 文件：addons/sourcemod/data/zsh_boss_points/%s.cfg",map);
        delete kv;return Plugin_Handled;
    }
    if(args!=1 || strlen(argument)!=1 || argument[0]<'1' || argument[0]>'5')
    {ReplyToCommand(client,"[Boss采点] 用法：!bosspoint 1~5，或 !bosspoint list 查看记录。");delete kv;return Plugin_Handled;}
    if(client<1 || !IsClientInGame(client) || IsFakeClient(client) || !IsPlayerAlive(client))
    {ReplyToCommand(client,"[Boss采点] 请由存活的真人玩家在游戏内记录。");delete kv;return Plugin_Handled;}
    if(!(GetEntityFlags(client)&FL_ONGROUND) || GetEntityMoveType(client)==MOVETYPE_NOCLIP)
    {ReplyToCommand(client,"[Boss采点] 请先关闭穿墙飞行并站稳在目标地面，再记录。");delete kv;return Plugin_Handled;}
    float origin[3],angles[3];GetClientAbsOrigin(client,origin);GetClientEyeAngles(client,angles);
    for(int axis=0;axis<3;axis++)if((view_as<int>(origin[axis])&0x7f800000)==0x7f800000 || FloatAbs(origin[axis])>15000.0)
    {ReplyToCommand(client,"[Boss采点] 当前位置超出配置允许范围，未写入。");delete kv;return Plugin_Handled;}
    if((view_as<int>(angles[1])&0x7f800000)==0x7f800000)
    {ReplyToCommand(client,"[Boss采点] 当前朝向无效，未写入。");delete kv;return Plugin_Handled;}
    angles[0]=0.0;angles[2]=0.0;
    while(angles[1]>180.0)angles[1]-=360.0;
    while(angles[1]<-180.0)angles[1]+=360.0;
    if(!DirExists(folder) && !CreateDirectory(folder,511))
    {ReplyToCommand(client,"[Boss采点] 无法创建记录目录。");delete kv;return Plugin_Handled;}
    char point[16],position[128],direction[128];Format(point,sizeof(point),"boss%s",argument);
    Format(position,sizeof(position),"%.6f %.6f %.6f",origin[0],origin[1],origin[2]);
    Format(direction,sizeof(direction),"0 %.6f 0",angles[1]);
    kv.Rewind();kv.JumpToKey(point,true);kv.SetString("origin",position);kv.SetString("angles",direction);kv.Rewind();
    if(!CommitCapturedPoints(kv,path))
    {ReplyToCommand(client,"[Boss采点] 保存失败，原记录或其备份已保留，请检查服务器日志。");delete kv;return Plugin_Handled;}
    int count=CapturedPointCount(kv);delete kv;
    ReplyToCommand(client,"[Boss采点] 已记录 %s（%d/5）：%s；朝向 %s。",point,count,position,direction);
    if(count==5)ReplyToCommand(client,"[Boss采点] 5个点已齐，可通知助手读取并检查位置；重录同编号可以修改。");
    LogMessage("[BossPoints] map=%s point=%s origin=%s angles=%s count=%d path=%s",map,point,position,direction,count,path);
    return Plugin_Handled;
}

public void OnPluginStart()
{
    if(CommandExists("sm_bosspoint")){SetFailState("sm_bosspoint already exists; refusing to override another command");return;}
    RegAdminCmd("sm_bosspoint",CaptureBossPoint,ADMFLAG_ROOT,"Temporary: record grounded position/yaw as point 1..5, or list saved points");
}
