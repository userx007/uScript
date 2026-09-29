// Build: g++ -std=c++20 -I<dds_typed/src> <your -I flags> qosxml_parser_tests.cpp -lddsc -ldl -lpthread   (unit tests of the built-in DDS-XML reader)
#include "dds_typed_driver.cpp"
#include <cstdio>
static int fails=0;
#define CHECK(c,m) do{ if(c) printf("  PASS  %s\n",m); else {printf("  FAIL  %s\n",m); ++fails;} }while(0)
static bool build(const std::string &xml, const char *prof, dds_qos_t *&r, dds_qos_t *&w, dds_qos_t *&t, std::vector<std::string> &warn, std::string &err)
{
  qosxml::Document d; std::string e;
  if (!qosxml::parseDocument(xml, d.root, e)) { err = "PARSE: " + e; return false; }
  if (!qosxml::indexProfiles(d, e)) { err = "INDEX: " + e; return false; }
  auto it = d.profiles.find(prof); if (it==d.profiles.end()) { err="NOPROFILE"; return false; }
  return qosxml::buildProfile(*it->second, warn, r, w, t, err);
}
int main(){
  std::vector<std::string> warn; std::string err; dds_qos_t *r,*w,*t;
  printf("[entities/CDATA/comments/namespaces/single quotes/self-closing]\n");
  { std::string x = "<?xml version='1.0'?><!DOCTYPE dds><dds:dds xmlns:dds='urn:x'><!-- c --><dds:qos_library name='L&amp;X'><qos_profile name=\"P\"><datawriter_qos><reliability><kind>RELIABLE_RELIABILITY_QOS</kind><max_blocking_time><sec>2</sec><nanosec>500</nanosec></max_blocking_time></reliability><history><kind>KEEP_LAST_HISTORY_QOS</kind><depth><![CDATA[ 7 ]]></depth></history><writer_data_lifecycle/></datawriter_qos></qos_profile></dds:qos_library></dds:dds>";
    bool ok = build(x, "L&X::P", r,w,t,warn,err); CHECK(ok, "parses; library name entity decoded ('L&X')");
    if (ok) { dds_reliability_kind_t rk; dds_duration_t mb; dds_history_kind_t hk; int32_t hd; bool ad;
      CHECK(w && dds_qget_reliability(w,&rk,&mb) && rk==DDS_RELIABILITY_RELIABLE && mb==2000000500LL, "max_blocking_time 2s+500ns");
      CHECK(w && dds_qget_history(w,&hk,&hd) && hk==DDS_HISTORY_KEEP_LAST && hd==7, "depth from CDATA = 7");
      CHECK(w && dds_qget_writer_data_lifecycle(w,&ad) && ad==true, "empty <writer_data_lifecycle/> -> default autodispose=true");
      CHECK(!r && !t, "no reader/topic section => nullptr (DDS defaults)"); } }
  printf("[error handling]\n");
  { warn.clear(); bool ok = build("<dds><qos_library name='L'><qos_profile name='P'><datareader_qos><reliability><kind>RELIABLE_QOS</kind></reliability></datareader_qos></qos_profile></qos_library></dds>","L::P",r,w,t,warn,err);
    CHECK(!ok && err.find("invalid reliability kind 'RELIABLE_QOS'")!=std::string::npos, "invalid enum value rejected with a clear message"); printf("        -> %s\n", err.c_str()); }
  { bool ok = build("<dds><qos_library name='L'><qos_profile name='P'><datawriter_qos><deadline><period/></deadline></datawriter_qos></qos_profile></qos_library></dds>","L::P",r,w,t,warn,err);
    CHECK(!ok && err.find("empty")!=std::string::npos, "empty <deadline><period/> rejected when that profile is used"); printf("        -> %s\n", err.c_str()); }
  { bool ok = build("<dds><qos_library name='L'><qos_profile name='P'><datawriter_qos><history><kind>KEEP_LAST_HISTORY_QOS</kind><depth>0</depth></history></datawriter_qos></qos_profile></qos_library></dds>","L::P",r,w,t,warn,err);
    CHECK(!ok, "KEEP_LAST with depth 0 rejected"); }
  { bool ok = build("<dds><qos_library name='L'><qos_profile name='P'><datawriter_qos><lifespan><duration><sec>-1</sec></duration></lifespan></datawriter_qos></qos_profile></qos_library></dds>","L::P",r,w,t,warn,err);
    CHECK(!ok, "negative duration rejected"); }
  { bool ok = build("<dds><qos_library name='L'><qos_profile name='P'><datawriter_qos><durability><kind>VOLATILE_DURABILITY_QOS</durability></datawriter_qos></qos_profile></qos_library></dds>","L::P",r,w,t,warn,err);
    CHECK(!ok && err.find("PARSE")==0, "mismatched tags -> XML syntax error"); printf("        -> %s\n", err.c_str()); }
  { bool ok = build("<dds><qos_library name='L'><qos_profile name='P'>","L::P",r,w,t,warn,err);
    CHECK(!ok && err.find("unclosed")!=std::string::npos, "unclosed element detected"); }
  { bool ok = build("<foo/>","L::P",r,w,t,warn,err); CHECK(!ok && err.find("expected <dds>")!=std::string::npos, "wrong root element rejected"); }
  { bool ok = build("<dds><qos_library name='L'><qos_profile name='P'/><qos_profile name='P'/></qos_library></dds>","L::P",r,w,t,warn,err); CHECK(!ok && err.find("twice")!=std::string::npos, "duplicate profile rejected"); }
  printf("[warnings: inapplicable / unsupported policies are skipped, not fatal]\n");
  { warn.clear(); bool ok = build("<dds><qos_library name='L'><qos_profile name='P'><datareader_qos><durability><kind>TRANSIENT_LOCAL_DURABILITY_QOS</kind></durability><writer_data_lifecycle/><user_data/></datareader_qos><publisher_qos><partition/></publisher_qos></qos_profile></qos_library></dds>","L::P",r,w,t,warn,err);
    CHECK(ok && warn.size()==3, "3 warnings (writer_data_lifecycle on reader, user_data, publisher_qos)"); for(auto&x:warn) printf("        WARN %s\n", x.c_str());
    dds_durability_kind_t dk; CHECK(r && dds_qget_durability(r,&dk) && dk==DDS_DURABILITY_TRANSIENT_LOCAL, "valid policy still applied"); }
  printf("[more policies map correctly]\n");
  { warn.clear(); bool ok = build("<dds><qos_library name='L'><qos_profile name='P'><datawriter_qos><liveliness><kind>MANUAL_BY_TOPIC_LIVELINESS_QOS</kind><lease_duration><sec>DURATION_INFINITY</sec></lease_duration></liveliness><ownership><kind>EXCLUSIVE_OWNERSHIP_QOS</kind></ownership><ownership_strength><value>9</value></ownership_strength><transport_priority><value>4</value></transport_priority><resource_limits><max_samples>100</max_samples><max_instances>LENGTH_UNLIMITED</max_instances></resource_limits><deadline><period><sec>1</sec></period></deadline></datawriter_qos></qos_profile></qos_library></dds>","L::P",r,w,t,warn,err);
    CHECK(ok, "liveliness/ownership/strength/priority/resource_limits/deadline build");
    if (ok) { dds_liveliness_kind_t lk; dds_duration_t ld,dl; dds_ownership_kind_t ok2; int32_t st,tp,ms,mi,mspi;
      CHECK(dds_qget_liveliness(w,&lk,&ld)&&lk==DDS_LIVELINESS_MANUAL_BY_TOPIC&&ld==DDS_INFINITY,"liveliness manual-by-topic, infinite lease");
      CHECK(dds_qget_ownership(w,&ok2)&&ok2==DDS_OWNERSHIP_EXCLUSIVE&&dds_qget_ownership_strength(w,&st)&&st==9,"exclusive ownership, strength 9");
      CHECK(dds_qget_transport_priority(w,&tp)&&tp==4,"transport_priority 4");
      CHECK(dds_qget_resource_limits(w,&ms,&mi,&mspi)&&ms==100&&mi==-1&&mspi==-1,"resource limits 100/unlimited/unlimited");
      CHECK(dds_qget_deadline(w,&dl)&&dl==DDS_SECS(1),"deadline 1s"); } }
  printf("\n%s (%d failure(s))\n", fails?"FAILED":"ALL PASSED", fails); return fails?1:0; }
