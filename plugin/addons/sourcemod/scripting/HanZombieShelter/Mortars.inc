// Separate recipe/menu identity, same C_Grenades / Kind_Grenade public API.
// Missing trajectory retains the original controlled grenade behavior.
bool MortarTrajectory(ConfigFile file,const char[] type)
{
    char profile[160],trajectory[24];Format(profile,sizeof(profile),"Types/%s/Attack",type);
    Text(file,profile,"trajectory",trajectory,sizeof(trajectory),"grenade");
    return StrEqual(trajectory,"mortar");
}

bool ValidateMortarType(ConfigFile file,const char[] type)
{
    if(file!=C_Grenades)return true;
    char profile[160],trajectory[24];Format(profile,sizeof(profile),"Types/%s/Attack",type);
    Text(file,profile,"trajectory",trajectory,sizeof(trajectory),"grenade");
    return StrEqual(trajectory,"grenade") || StrEqual(trajectory,"mortar") || ConfigFailure("grenade Attack trajectory must be grenade or mortar");
}

// Solve |delta/t + (0,0,g*t/2)| = speed for the longer positive flight.
// This also handles a vertically aligned target without dividing by XY range.
// No solution, zero gravity or an expired high arc means no shot; never replace
// a requested high throw with an instant hit, straight beam or an imaginary root.
bool MortarLaunchVelocity(const float muzzle[3],const float target[3],float speed,float gravity,float lifetime,float velocity[3],float &flight)
{
    if(!(speed>=1.0 && speed<=10000.0 && gravity>0.0 && gravity<=1000.0 && lifetime>=0.5 && lifetime<=15.0))return false;
    float delta[3];SubtractVectors(target,muzzle,delta);
    for(int a=0;a<3;a++)if(!(delta[a]>=-30000.0 && delta[a]<=30000.0))return false;
    float speedSquared=speed*speed,height=delta[2],horizontal=delta[0]*delta[0]+delta[1]*delta[1];
    float discriminant=speedSquared*speedSquared-gravity*(gravity*horizontal+2.0*height*speedSquared);
    if(!(discriminant>=0.0))return false;
    float timeSquared=2.0*(speedSquared-gravity*height+SquareRoot(discriminant))/(gravity*gravity);
    if(!(timeSquared>0.0))return false;
    flight=SquareRoot(timeSquared);
    if(!(flight>=0.05 && flight<lifetime))return false;
    for(int a=0;a<3;a++)velocity[a]=delta[a]/flight;
    velocity[2]+=0.5*gravity*flight;
    return velocity[2]>0.0;
}

// Same TE method as unchanged HZS BeamFollowCreate, using the already cached
// Valve laserbeam material. No sprite/entity/download is allocated per trail.
void GrenadeProjectileTrail(int entity,bool mortar)
{
    if(entity<=MaxClients || !IsValidEntity(entity) || gBeamSprite<=0)return;
    int color[4]={255,30,30,200};if(mortar){color[1]=255;color[2]=255;}
    TE_SetupBeamFollow(entity,gBeamSprite,0,1.0,10.0,10.0,5,color);TE_SendToAll();
}

void MortarImpactRing(const float position[3],float radius)
{
    if(gBeamSprite<=0 || !(radius>0.0 && radius<=8192.0))return;
    int white[4]={255,255,255,160};
    TE_SetupBeamRingPoint(position,8.0,radius*2.0,gBeamSprite,0,0,0,0.45,1.5,0.0,white,0,0);TE_SendToAll();
}
