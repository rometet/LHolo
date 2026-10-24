#include "place/ExternalRotationDeliveryPolicy.h"
#include <cstdio>
#include <cmath>
#include <array>
using namespace lholo::place::detail;
int checks=0,failures=0;
void ck(bool ok,char const* msg){++checks;if(!ok){++failures;std::printf("FAIL %s\n",msg);}}
int main(){
 ExternalRotationWarmup s;
 ck(!s.ready(100),"no placement on initial tick");s.forwardedPrime(100);
 ck(!s.ready(100),"duplicate first tick cannot place");s.forwardedPrime(100);
 ck(s.primes==1,"duplicate does not count as second prime");
 ck(!s.ready(101),"second fresh tick remains orientation-only");s.forwardedPrime(101);
 ck(!s.ready(101),"cannot place on a reused warmup tick");
 ck(s.ready(102),"third native tick can place");ck(!s.ready(99),"out-of-order tick never places");
 s.reset();ck(!s.ready(1000)&&s.primes==0,"native action conflict resets pre-roll");
 s.forwardedPrime(0);s.forwardedPrime(1);ck(s.ready(2),"zero initial tick supported");
 for(float a:std::array<float,7>{-180,-90,-15,0,45,90,180})for(float b:std::array<float,5>{-180,-90,0,90,180})
 for(auto v:std::array<ExternalMoveVector,5>{{{0,0},{1,0},{0,1},{-.7f,.3f},{.2f,-1}}}){
  auto r=preserveWorldMove(v.x,v.y,a,b);auto inv=preserveWorldMove(r.x,r.y,b,a);
  ck(std::fabs(inv.x-v.x)<.00001f&&std::fabs(inv.y-v.y)<.00001f,"movement inverse roundtrip");
  float ar=a*.0174532925199433f,br=b*.0174532925199433f;
  float ax=std::cos(ar)*v.x-std::sin(ar)*v.y,az=std::sin(ar)*v.x+std::cos(ar)*v.y;
  float bx=std::cos(br)*r.x-std::sin(br)*r.y,bz=std::sin(br)*r.x+std::cos(br)*r.y;
  ck(std::fabs(ax-bx)<.00001f&&std::fabs(az-bz)<.00001f,"world-space movement invariant");
 }
 std::printf("External rotation sequence/movement checks=%d failures=%d; native BDS verification separate\n",checks,failures);
 return failures?1:0;
}
