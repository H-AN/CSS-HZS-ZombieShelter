// A day is booked once at the rate in force when first reached. Changing the
// cvar settles the current day at its old rate, then affects only future days.
ConVar gSkillsDailyCvar;
int gSkillsDailyRate=1,gSkillsEarned;
ArrayList gSkillsRateSchedule; // Sparse [first eligible day, rate] audit rows.

void RegisterSkillsBudgetCvar()
{
    gSkillsDailyCvar=CreateConVar("sm_zsh_skill_points_per_day","-1",
        "Skill points on each NEW highest day: -1 uses map skills.cfg, 0 pauses future awards, 1..1000000 overrides. Changes never recalculate past days.",
        FCVAR_NOTIFY,true,-1.0,true,1000000.0);
    gSkillsDailyCvar.AddChangeHook(SkillsDailyRateChanged);
}

void SkillsBudgetReset()
{
    gSkillsHighestDay=0;gSkillsEarned=0;gSkillsDailyRate=gSkillsPointsPerDay;
    delete gSkillsRateSchedule;gSkillsRateSchedule=new ArrayList(2);
    // Skill accounts and the historical schedule both start fresh per map.
    if(gSkillsDailyCvar!=null)gSkillsDailyCvar.IntValue=-1;
}

void SkillsBudgetConfigReady()
{
    int overrideRate=gSkillsDailyCvar==null ? -1 : gSkillsDailyCvar.IntValue;
    gSkillsDailyRate=overrideRate<0 ? gSkillsPointsPerDay : overrideRate;
    SkillsBudgetSchedule(gSkillsDailyRate);
}

int SkillsDailyRate(){return gSkillsDailyRate;}

void SkillsBudgetSchedule(int rate)
{
    if(gSkillsRateSchedule==null)gSkillsRateSchedule=new ArrayList(2);
    if(gSkillsHighestDay>=SKILL_CELL_LIMIT)return;
    int row[2];row[0]=gSkillsHighestDay+1;row[1]=rate;
    int last=gSkillsRateSchedule.Length-1;
    if(last>=0)
    {
        if(gSkillsRateSchedule.Get(last,1)==rate)return;
        if(gSkillsRateSchedule.Get(last,0)==row[0]){gSkillsRateSchedule.SetArray(last,row,sizeof(row));return;}
    }
    gSkillsRateSchedule.PushArray(row,sizeof(row));
}

public void SkillsDailyRateChanged(ConVar cvar,const char[] oldValue,const char[] newValue)
{
    if(!gSkillsConfigReady)return;
    SkillsObserveDay(); // gSkillsDailyRate still holds the old effective rate.
    int rate=cvar.IntValue<0 ? gSkillsPointsPerDay : cvar.IntValue;
    if(rate==gSkillsDailyRate)return;
    int before=gSkillsDailyRate;gSkillsDailyRate=rate;SkillsBudgetSchedule(rate);
    gSkillsConfigRevision++; // Invalidate every outstanding skill/admin quote.
    LogMessage("[ShelterSkills] daily rate %d -> %d; earned=%d highest=%d; future new days only",before,rate,gSkillsEarned,gSkillsHighestDay);
}

// Pure saturating formula retained for private math checks; live awards use the
// historical ledger below, never highest * current rate.
stock int SkillsQuotaFor(int highest,int limit,int daily)
{
    if(highest<1 || limit<0 || daily<1)return 0;
    int days=limit>0 && highest>limit ? limit : highest;
    return days>SKILL_CELL_LIMIT/daily ? SKILL_CELL_LIMIT : days*daily;
}

int SkillsAccrueDays(int earned,int oldHighest,int newHighest,int limit,int daily)
{
    if(earned<0 || earned>SKILL_CELL_LIMIT || oldHighest<0 || newHighest<=oldHighest || limit<0 || daily<1)return earned;
    int first=oldHighest,last=newHighest;
    if(limit>0){if(first>limit)first=limit;if(last>limit)last=limit;}
    int days=last-first;if(days<1)return earned;
    int room=SKILL_CELL_LIMIT-earned;
    return days>room/daily ? SKILL_CELL_LIMIT : earned+days*daily;
}

void SkillsObserveDay()
{
    if(gSkillsConfigReady && gEnabled && Running())SkillsRecordDay(gDay);
}

void SkillsRecordDay(int day)
{
    if(day<=gSkillsHighestDay || day<1 || day>SKILL_CELL_LIMIT)return;
    gSkillsEarned=SkillsAccrueDays(gSkillsEarned,gSkillsHighestDay,day,gSurvivalDayLimit,gSkillsDailyRate);
    gSkillsHighestDay=day;
}

int SkillsQuota(){SkillsObserveDay();return gSkillsEarned;}
int SkillsEarnedQuota(){return SkillsQuota();}

int SkillsAccountBudget(int account,int quota)
{
    if(gSkillsRows==null || account<0 || account>=gSkillsRows.Length || quota<0 || quota>SKILL_CELL_LIMIT)return 0;
    int adjustment=gSkillsRows.Get(account,SKILL_ADMIN);
    if(adjustment>0)return adjustment>SKILL_CELL_LIMIT-quota ? SKILL_CELL_LIMIT : quota+adjustment;
    return adjustment< -quota ? 0 : quota+adjustment;
}

int SkillsAdminAdjustment(int client)
{
    int account=SkillsAccountIndex(client);return account<0 ? 0 : gSkillsRows.Get(account,SKILL_ADMIN);
}

// Pure account mutation for server-owned callers. Production's public entry
// below adds actor, identity and busy guards; spent levels are never removed.
bool SkillsAccountAdjust(int account,int delta,int expectedRevision,int quota,int &actual)
{
    actual=0;
    if(gSkillsRows==null || account<0 || account>=gSkillsRows.Length || delta==0
        || delta< -SKILL_CELL_LIMIT || delta>SKILL_CELL_LIMIT || expectedRevision<1 || expectedRevision>=SKILL_CELL_LIMIT
        || gSkillsRows.Get(account,SKILL_SESSION)!=gSkillsMapSession
        || gSkillsRows.Get(account,SKILL_REVISION)!=expectedRevision || quota<0 || quota>SKILL_CELL_LIMIT)return false;
    int budget=SkillsAccountBudget(account,quota),spent=gSkillsRows.Get(account,SKILL_SPENT);
    if(budget<spent)return false;
    int room=delta>0 ? SKILL_CELL_LIMIT-budget : budget-spent;
    actual=delta>0 ? (delta>room ? room : delta) : (delta< -room ? -room : delta);
    if(actual==0)return false;
    int adjustment=gSkillsRows.Get(account,SKILL_ADMIN);
    // Saturated earned totals can leave a positive adjustment hidden beyond the
    // currency ceiling. Normalize only that hidden excess before a mutation.
    if(adjustment>0 && adjustment>SKILL_CELL_LIMIT-quota)adjustment=SKILL_CELL_LIMIT-quota;
    gSkillsRows.Set(account,expectedRevision+1,SKILL_REVISION);
    gSkillsRows.Set(account,adjustment+actual,SKILL_ADMIN);
    return true;
}

bool SkillsAdminAdjust(int actor,int target,int delta,int expectedAccount,int expectedRevision,int &actual)
{
    actual=0;SkillsObserveDay();
    if(!gSkillsEnabled || !gSkillsConfigReady || !Running() || gTerminal || !LiveHuman(actor) || IsFakeClient(actor)
        || !IsShelterAdmin(actor) || target<1 || target>MaxClients || !SkillsIdentity(target)
        || SkillsAccountIndex(target)!=expectedAccount || gSkillsBusy[target]
        || SkillsAccountRevision(target)!=expectedRevision)return false;
    gSkillsBusy[target]=true;
    bool success=SkillsAccountAdjust(expectedAccount,delta,expectedRevision,SkillsQuota(),actual);
    gSkillsBusy[target]=false;
    if(success)LogAction(actor,target,"[ShelterSkills] adjusted unused points requested=%d actual=%d balance=%d spent=%d personal_adjustment=%d account=%d revision=%d",
        delta,actual,SkillsBalance(target),SkillsSpent(target),SkillsAdminAdjustment(target),expectedAccount,SkillsAccountRevision(target));
    return success;
}
