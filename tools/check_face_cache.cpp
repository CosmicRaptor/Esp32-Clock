#include "../firmware/BhoomiClock/face_cache_checks.h"
#include <cassert>
#include <cstring>
#include <map>
#include <string>
struct FS {
  std::map<std::string,int> files; bool failStage=false, failRestore=false;
  bool exists(const std::string &p) { return files.count(p); }
  bool remove(const std::string &p) { return files.erase(p)!=0; }
  bool rename(const std::string &a,const std::string &b) {
    if ((failStage && a=="next") || (failRestore && a=="bak")) return false;
    auto it=files.find(a); if(it==files.end() || files.count(b)) return false;
    files[b]=it->second; files.erase(it); return true;
  }
};
int main() {
  FS ok; ok.files={{"old",1},{"next",2}};
  assert(facecache::promote(ok,std::string("old"),std::string("next"),std::string("bak")));
  assert(ok.files.at("old")==2 && !ok.exists("bak"));
  FS failed; failed.files={{"old",1},{"next",2}}; failed.failStage=true;
  assert(!facecache::promote(failed,std::string("old"),std::string("next"),std::string("bak")));
  assert(failed.files.at("old")==1); // a failed replacement preserves the previous face
  FS interrupted; interrupted.files={{"bak",1},{"next",2}}; interrupted.failStage=true; interrupted.failRestore=true;
  assert(!facecache::promote(interrupted,std::string("old"),std::string("next"),std::string("bak")));
  assert(interrupted.files.at("bak")==1); // failed recovery retains the backup
  FS resumed; resumed.files={{"bak",1},{"next",2}};
  assert(facecache::promote(resumed,std::string("old"),std::string("next"),std::string("bak")));
  assert(resumed.files.at("old")==2 && !resumed.exists("bak")); // reset between renames
  FS absent; absent.files={{"old",1}};
  assert(!facecache::promote(absent,std::string("old"),std::string("next"),std::string("bak")));
  assert(absent.files.at("old")==1); // an absent replacement cannot remove the old face
  uint8_t sha[32] = {0xab, 0xcd, 0x01, 0xef, 0x23, 0x45, 0x67, 0x89};
  assert(facecache::digestMatchesHex(sha, "abcd01ef23456789", 16));
  assert(!facecache::digestMatchesHex(sha, "abcd01ef23456788", 16));
  assert(!facecache::digestMatchesHex(sha, "abcd01ef2345678", 16));
  uint8_t other[32]; memcpy(other, sha, 32); assert(facecache::digestMatches(sha, other));
  other[31] = 1; assert(!facecache::digestMatches(sha, other));
  uint8_t jpeg[12] = {0xff,0xd8};
  assert(facecache::validAssetShape(jpeg, 12, 12, 320, 240, true));
  jpeg[0]=0; assert(!facecache::validAssetShape(jpeg, 12, 12, 320, 240, true));
  uint8_t sprite[24] = {0x19,0x14,0,0,2,0,2,0};
  assert(facecache::validAssetShape(sprite, 24, 24, 2, 2, false));
  assert(!facecache::validAssetShape(sprite, 24, 24, 3, 2, false));
  assert(!facecache::validAssetShape(sprite, 23, 24, 2, 2, false));
}
