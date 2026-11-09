#include "wait_profile.h"
#include <pthread.h>
#include <cstring>
extern "C" void av2_boot_log(const char*,const char*,...);
namespace {
struct Key {uint32_t thread,object,reason,mode,alertable,result;bool timed;uint64_t timeout;
 bool operator==(const Key& b)const{return thread==b.thread&&object==b.object&&reason==b.reason&&mode==b.mode&&alertable==b.alertable&&result==b.result&&timed==b.timed&&timeout==b.timeout;}};
struct Row {Key key{};uint64_t count=0,total=0,max=0;};
Row rows[128]{};unsigned used=0,dumps=0;uint64_t overflow=0,fast=0,invalid=0;
pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
}
extern "C" void av2_wait_record(uint32_t thread,uint32_t object,bool timed,uint64_t timeout,uint32_t reason,uint32_t mode,uint32_t alertable,uint32_t result,uint64_t start,uint64_t end) noexcept {
 if(!start)return;pthread_mutex_lock(&mutex);
 if(!end||end<start){++invalid;pthread_mutex_unlock(&mutex);return;}
 const uint64_t elapsed=end-start;if(elapsed<100000){++fast;pthread_mutex_unlock(&mutex);return;}
 const Key key{thread,object,reason,mode,alertable,result,timed,timed?timeout:0};unsigned i=0;while(i<used&&!(rows[i].key==key))++i;
 if(i==used){if(used==128){++overflow;pthread_mutex_unlock(&mutex);return;}rows[used++].key=key;}
 auto& row=rows[i];++row.count;row.total+=elapsed;if(elapsed>row.max)row.max=elapsed;pthread_mutex_unlock(&mutex);
}
extern "C" void av2_wait_dump(const char* window) noexcept {
 Row copy[128];unsigned n;uint64_t dropped,below,bad;
 pthread_mutex_lock(&mutex);if(dumps==3){pthread_mutex_unlock(&mutex);return;}++dumps;n=used;std::memcpy(copy,rows,n*sizeof(Row));dropped=overflow;below=fast;bad=invalid;pthread_mutex_unlock(&mutex);
 for(unsigned i=0;i<n;++i)for(unsigned j=i+1;j<n;++j)if(copy[j].total>copy[i].total){const Row tmp=copy[i];copy[i]=copy[j];copy[j]=tmp;}
 av2_boot_log("summary","wait-profile window=%s call=KeWaitForSingleObject inclusive=1 floor-us=100 groups=%u overflow=%llu below-floor=%llu rejected-duration=%llu active-omitted=1",window,n,(unsigned long long)dropped,(unsigned long long)below,(unsigned long long)bad);
 for(unsigned i=0;i<n&&i<12;++i){const auto& v=copy[i];const auto& k=v.key;
  av2_boot_log("summary","wait rank=%u thread=%08x object=%08x wait-limit-kind=%s wait-limit-100ns-bits=%016llx reason=%u mode=%u alertable=%u result=%08x calls=%llu total-ms=%.3f max-ms=%.3f",i+1,k.thread,k.object,!k.timed?"infinite":!k.timeout?"poll":(k.timeout>>63)?"relative":"absolute",(unsigned long long)k.timeout,k.reason,k.mode,k.alertable,k.result,(unsigned long long)v.count,double(v.total)/1e6,double(v.max)/1e6);
 }
}
