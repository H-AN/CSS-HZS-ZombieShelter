// Resource damage is consumed by the logical harvesting hook, so explicitly
// supply a small hit effect without invoking Impact's material sound again.
void ResourceImpactGeometry(int entity,const float eyes[3],const float angles[3],const float damagePosition[3],float point[3],float normal[3])
{
    float mins[3],maxs[3],origin[3],bodyAngles[3];
    GetEntPropVector(entity,Prop_Data,"m_vecAbsOrigin",origin);
    GetEntPropVector(entity,Prop_Data,"m_angAbsRotation",bodyAngles);
    ModelWorldBounds(entity,origin,bodyAngles,mins,maxs);
    bool supplied=damagePosition[0]!=0.0 || damagePosition[1]!=0.0 || damagePosition[2]!=0.0;
    for(int a=0;a<3;a++)
    {
        if((view_as<int>(damagePosition[a])&0x7f800000)==0x7f800000
            || damagePosition[a]<mins[a]-32.0 || damagePosition[a]>maxs[a]+32.0)supplied=false;
    }
    float direction[3],rayEnd[3];GetAngleVectors(angles,direction,NULL_VECTOR,NULL_VECTOR);
    float reach=GetVectorDistance(eyes,origin)+128.0;
    for(int a=0;a<3;a++)rayEnd[a]=eyes[a]+direction[a]*reach;
    Handle trace=TR_ClipRayToEntityEx(eyes,rayEnd,MASK_SHOT,RayType_EndPoint,entity);
    bool hit=TR_DidHit(trace);
    if(hit){TR_GetEndPosition(point,trace);TR_GetPlaneNormal(trace,normal);}
    delete trace;
    // Knife damage positions can themselves be swept-hull centres. Validate
    // the supplied point with its own ray: a valid off-centre contact need not
    // coincide with the central view ray, but a floating hull centre is rejected.
    if(supplied)
    {
        float toPoint[3],end[3];MakeVectorFromPoints(eyes,damagePosition,toPoint);NormalizeVector(toPoint,toPoint);
        for(int a=0;a<3;a++)end[a]=damagePosition[a]+toPoint[a]*8.0;
        trace=TR_ClipRayToEntityEx(eyes,end,MASK_SHOT,RayType_EndPoint,entity);
        if(TR_DidHit(trace) && !TR_StartSolid(trace))
        {
            float contact[3];TR_GetEndPosition(contact,trace);
            if(GetVectorDistance(contact,damagePosition)<=2.0)
            {point=damagePosition;TR_GetPlaneNormal(trace,normal);hit=true;}
        }
        delete trace;
    }
    if(!hit)
    {
        // A knife hull can hit a corner that its central ray misses. Clip only
        // against the entity that actually received damage; never another prop.
        float hullMin[3]={-16.0,-16.0,-16.0},hullMax[3]={16.0,16.0,16.0};
        trace=TR_ClipRayHullToEntityEx(eyes,rayEnd,hullMin,hullMax,MASK_SHOT,entity);
        hit=TR_DidHit(trace);
        if(hit){TR_GetEndPosition(point,trace);TR_GetPlaneNormal(trace,normal);}
        delete trace;
        if(hit)
        {
            // A swept hull end position is its centre, outside the struck
            // surface. Resolve a real contact rather than floating sparks.
            float centre[3],bestPoint[3],bestNormal[3],best=999999.0;centre=point;
            bool refined;
            for(int probe=-1;probe<8;probe++)
            {
                float end[3];
                if(probe<0)
                {
                    float support=16.0*(FloatAbs(normal[0])+FloatAbs(normal[1])+FloatAbs(normal[2]))+4.0;
                    for(int a=0;a<3;a++)end[a]=centre[a]-normal[a]*support;
                }
                else for(int a=0;a<3;a++)end[a]=centre[a]+((probe&(1<<a)) ? 20.0 : -20.0);
                trace=TR_ClipRayToEntityEx(centre,end,MASK_SHOT,RayType_EndPoint,entity);
                if(TR_DidHit(trace) && !TR_StartSolid(trace))
                {
                    float contact[3];TR_GetEndPosition(contact,trace);
                    float distance=GetVectorDistance(centre,contact,true);
                    if(distance<best)
                    {best=distance;bestPoint=contact;TR_GetPlaneNormal(trace,bestNormal);refined=true;}
                }
                delete trace;
            }
            if(refined){point=bestPoint;normal=bestNormal;}
            else
            {
                float support=16.0*(FloatAbs(normal[0])+FloatAbs(normal[1])+FloatAbs(normal[2]));
                for(int a=0;a<3;a++)point[a]-=normal[a]*support;
            }
        }
        if(!hit){if(supplied)point=damagePosition;else EntityCenter(entity,point);}
    }
    if(!hit || GetVectorLength(normal)<0.01)
    {MakeVectorFromPoints(point,eyes,normal);NormalizeVector(normal,normal);}
    for(int a=0;a<3;a++)point[a]+=normal[a]*1.5;
}

void ResourceMeleeFeedback(int entity,int attacker,int inflictor,const float damagePosition[3])
{
    if(!ResourceMeleeHit(attacker,inflictor))return;
    float eyes[3],angles[3],point[3],normal[3];
    GetClientEyePosition(attacker,eyes);GetClientEyeAngles(attacker,angles);
    ResourceImpactGeometry(entity,eyes,angles,damagePosition,point,normal);
    PlayResourceImpactSound(attacker,inflictor,entity,point);
    TE_SetupSparks(point,normal,1,2);
    TE_SendToAllInRange(point,RangeType_Visibility);
}
