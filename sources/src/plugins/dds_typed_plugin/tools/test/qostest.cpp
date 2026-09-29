// End-to-end test of QOS_PROFILE_FILE support using the REAL DdsTypedDriver,
// the REAL Cyclone DDS 11.0.1, a real idlc-generated type plugin and the
// uploaded XML files. A raw Cyclone participant inspects the QoS each
// endpoint actually advertises on the (discovery) wire.
#include "dds_typed_driver.hpp"
#include "uLogger.hpp"
#include "dds/dds.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

static const char *U   = "/mnt/user-data/uploads/";
static int g_fail      = 0;
#define CHECK(cond, msg) do { if (cond) printf("   PASS  %s\n", msg); else { printf("   FAIL  %s\n", msg); ++g_fail; } } while (0)

static std::string P(const char *f) { return std::string(U) + f; }

static DdsTypedDriver::Config baseCfg(const std::string &name)
{
    DdsTypedDriver::Config c;
    c.domainId           = 42;
    c.strInstanceName    = name;
    c.participantName    = name;
    c.cycloneConfigFile  = P("cyclonedds-loopback.xml");
    c.preloadPluginPaths = {"/tmp/t/gen/libsimple_types.so"};
    return c;
}

static void cmd(DdsTypedDriver &d, const std::string &text)
{
    std::span<const uint8_t> s(reinterpret_cast<const uint8_t *>(text.data()), text.size());
    d.send(1000, s, "", std::stop_token{});
}

struct EpQos { bool found=false; bool writer=false; int rel=-1, dur=-1, dord=-1; };

// Newest endpoint (of the requested kind) on `topic` seen by the raw participant.
static EpQos lookup(dds_entity_t rd, const char *topic, bool wantWriter)
{
    EpQos r;
    void *s[16] = {}; dds_sample_info_t si[16];
    (void)wantWriter;
    int n = dds_read(rd, s, si, 16, 16);
    for (int i = 0; i < n; ++i) {
        auto *e = (dds_builtintopic_endpoint_t *)s[i];
        if (si[i].instance_state != DDS_IST_ALIVE || !si[i].valid_data || strcmp(e->topic_name, topic)) continue;
        dds_reliability_kind_t rk; dds_duration_t mb; dds_durability_kind_t dk; dds_destination_order_kind_t dok;
        r.found = true;
        if (dds_qget_reliability(e->qos,&rk,&mb)) r.rel = (int)rk;
        if (dds_qget_durability(e->qos,&dk))      r.dur = (int)dk;
        if (dds_qget_destination_order(e->qos,&dok)) r.dord = (int)dok;
    }
    dds_return_loan(rd, s, n);
    return r;
}

static void drain(dds_entity_t rd) { void *s[16]={}; dds_sample_info_t si[16]; int n; while ((n = dds_take(rd,s,si,16,16))>0) dds_return_loan(rd,s,n); }

int main()
{
    LOG_INIT(LOG_DEBUG, LOG_DEBUG, false, false, false, false);
    // Raw observer in the same domain (the first driver creates it with the loopback file).
    setenv("CYCLONEDDS_URI", ("file://" + P("cyclonedds-loopback.xml")).c_str(), 1);
    dds_entity_t pp   = dds_create_participant(42, nullptr, nullptr);
    dds_entity_t pubs = dds_create_reader(pp, DDS_BUILTIN_TOPIC_DCPSPUBLICATION, nullptr, nullptr);
    dds_entity_t subs = dds_create_reader(pp, DDS_BUILTIN_TOPIC_DCPSSUBSCRIPTION, nullptr, nullptr);
    const char *T = "vehicle/state";
    auto settle = [] { std::this_thread::sleep_for(std::chrono::milliseconds(600)); };

    printf("\n[S1] no QoS file -> legacy built-in QoS (best-effort, volatile)\n");
    {
        drain(pubs);
        auto c = baseCfg("s1"); DdsTypedDriver d(c); CHECK(d.open(), "open()");
        cmd(d, std::string("PUBLISH ") + T + " id=1,label=a,speed=1"); settle();
        auto e = lookup(pubs, T, true);
        CHECK(e.found, "writer discovered");
        CHECK(e.rel == DDS_RELIABILITY_BEST_EFFORT, "reliability BEST_EFFORT");
        CHECK(e.dur == DDS_DURABILITY_VOLATILE, "durability VOLATILE");
        d.close();
    }

    printf("\n[S2] qf + qd=StatePattern -> RELIABLE / TRANSIENT_LOCAL / BY_SOURCE_TIMESTAMP\n");
    {
        drain(pubs);
        auto c = baseCfg("s2"); c.qosProfileFile = P("USER_QOS_PROFILES_Cyclone.xml"); c.qosDefaultProfile = "StatePattern";
        DdsTypedDriver d(c); CHECK(d.open(), "open()");
        cmd(d, std::string("PUBLISH ") + T + " id=1,label=a,speed=1"); settle();
        auto e = lookup(pubs, T, true);
        CHECK(e.found, "writer discovered");
        CHECK(e.rel == DDS_RELIABILITY_RELIABLE, "reliability RELIABLE");
        CHECK(e.dur == DDS_DURABILITY_TRANSIENT_LOCAL, "durability TRANSIENT_LOCAL");
        CHECK(e.dord == DDS_DESTINATIONORDER_BY_SOURCE_TIMESTAMP, "destination_order BY_SOURCE_TIMESTAMP");
        std::span<uint8_t> none; (void)none;
        d.close();
    }

    printf("\n[S3] qt glob rule wins over qd (first match): vehicle/* -> PeriodicPattern (best-effort)\n");
    {
        drain(pubs);
        auto c = baseCfg("s3"); c.qosProfileFile = P("USER_QOS_PROFILES_Cyclone.xml"); c.qosDefaultProfile = "StatePattern";
        c.qosTopicProfiles = "vehicle/*=PeriodicPattern";
        DdsTypedDriver d(c); CHECK(d.open(), "open()");
        cmd(d, std::string("PUBLISH ") + T + " id=1,label=a,speed=1"); settle();
        auto e = lookup(pubs, T, true);
        CHECK(e.rel == DDS_RELIABILITY_BEST_EFFORT, "reliability BEST_EFFORT (PeriodicPattern)");
        d.close();
    }

    printf("\n[S4] rule order: specific AlarmPattern before broad Periodic; fully-qualified name; ',' separator\n");
    {
        drain(pubs);
        auto c = baseCfg("s4"); c.qosProfileFile = P("USER_QOS_PROFILES_Cyclone.xml");
        c.qosTopicProfiles = "vehicle/state=DDSDefaultQoSLibrary::AlarmPattern,vehicle/*=PeriodicPattern";
        DdsTypedDriver d(c); CHECK(d.open(), "open()");
        cmd(d, std::string("PUBLISH ") + T + " id=1,label=a,speed=1"); settle();
        auto e = lookup(pubs, T, true);
        CHECK(e.rel == DDS_RELIABILITY_RELIABLE && e.dur == DDS_DURABILITY_TRANSIENT_LOCAL, "AlarmPattern applied (RELIABLE, TRANSIENT_LOCAL)");
        d.close();
    }

    printf("\n[S5] topic not matched and no default -> legacy QoS; empty 'Default' profile -> DDS defaults\n");
    {
        drain(pubs);
        auto c = baseCfg("s5"); c.qosProfileFile = P("USER_QOS_PROFILES_Cyclone.xml"); c.qosTopicProfiles = "other/*=AlarmPattern";
        DdsTypedDriver d(c); CHECK(d.open(), "open()");
        cmd(d, std::string("PUBLISH ") + T + " id=1,label=a,speed=1"); settle();
        auto e = lookup(pubs, T, true);
        CHECK(e.rel == DDS_RELIABILITY_BEST_EFFORT, "unmatched topic keeps built-in best-effort");
        d.close();
        drain(pubs);
        auto c2 = baseCfg("s5b"); c2.qosProfileFile = P("USER_QOS_PROFILES_Cyclone.xml"); c2.qosDefaultProfile = "Default";
        DdsTypedDriver d2(c2); CHECK(d2.open(), "open() with the empty 'Default' profile");
        cmd(d2, std::string("PUBLISH ") + T + " id=1,label=a,speed=1"); settle();
        auto e2 = lookup(pubs, T, true);
        CHECK(e2.found && e2.rel == DDS_RELIABILITY_RELIABLE && e2.dur == DDS_DURABILITY_VOLATILE, "DDS defaults: RELIABLE writer, VOLATILE");
        d2.close();
    }

    printf("\n[S6] failures must refuse to open (no silent fallback)\n");
    {
        auto c = baseCfg("s6a"); c.qosProfileFile = P("USER_QOS_PROFILES_Cyclone.xml"); c.qosDefaultProfile = "NoSuchPattern";
        DdsTypedDriver d(c); CHECK(!d.open(), "unknown profile name -> open() fails");
#if defined(DDS_HAS_QOS_PROVIDER) && !defined(DDS_TYPED_BUILTIN_QOS_PARSER)
        // Cyclone's provider rejects the whole Consolidated file.
        auto c2 = baseCfg("s6b"); c2.qosProfileFile = P("USER_QOS_PROFILES_Consolidated.xml"); c2.qosDefaultProfile = "StatePattern";
        DdsTypedDriver d2(c2); CHECK(!d2.open(), "Consolidated.xml (not parseable by Cyclone's provider) -> open() fails");
#else
        // Built-in reader: only the selected profile is validated.
        auto c2 = baseCfg("s6b"); c2.qosProfileFile = P("USER_QOS_PROFILES_Consolidated.xml"); c2.qosDefaultProfile = "StatePattern";
        DdsTypedDriver d2(c2); CHECK(d2.open(), "Consolidated.xml + complete profile (StatePattern) -> opens (built-in reader)");
        d2.close();
        auto c2b = baseCfg("s6b2"); c2b.qosProfileFile = P("USER_QOS_PROFILES_Consolidated.xml"); c2b.qosDefaultProfile = "PeriodicPattern";
        DdsTypedDriver d2b(c2b); CHECK(!d2b.open(), "Consolidated.xml + template profile (PeriodicPattern, empty deadline) -> open() fails");
#endif
        auto c3 = baseCfg("s6c"); c3.qosProfileFile = "/nonexistent.xml"; c3.qosDefaultProfile = "StatePattern";
        DdsTypedDriver d3(c3); CHECK(!d3.open(), "missing file -> open() fails");
        auto c4 = baseCfg("s6d"); c4.qosProfileFile = P("USER_QOS_PROFILES_Cyclone.xml"); c4.qosTopicProfiles = "vehicle/state";
        DdsTypedDriver d4(c4); CHECK(!d4.open(), "malformed QOS_TOPIC_PROFILES -> open() fails");
        auto c5 = baseCfg("s6e"); c5.qosProfileFile = P("cyclonedds-loopback.xml"); c5.qosDefaultProfile = "StatePattern";
        DdsTypedDriver d5(c5); CHECK(!d5.open(), "a Cyclone config file given as QoS file -> open() fails");
    }

    printf("\n[S7] functional: TRANSIENT_LOCAL latching — a late subscriber gets the sample published earlier\n");
    {
        auto c = baseCfg("s7pub"); c.qosProfileFile = P("USER_QOS_PROFILES_Cyclone.xml"); c.qosDefaultProfile = "StatePattern";
        c.participantName = "s7pub";
        DdsTypedDriver pub(c); CHECK(pub.open(), "publisher open()");
        cmd(pub, std::string("PUBLISH ") + T + " id=77,label=latched,speed=3.5");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        auto c2 = baseCfg("s7sub"); c2.qosProfileFile = c.qosProfileFile; c2.qosDefaultProfile = "StatePattern";
        DdsTypedDriver sub(c2); CHECK(sub.open(), "subscriber open()");
        cmd(sub, std::string("SUBSCRIBE ") + T);
        uint8_t buf[512] = {};
        auto rr = sub.receive(3000, std::span<uint8_t>(buf, sizeof buf), {}, std::string_view(""), std::stop_token{});
        std::string got(reinterpret_cast<char *>(buf), rr.bytes_read);
        printf("   received: '%s'\n", got.c_str());
        CHECK(got.find("id=77") != std::string::npos && got.find("latched") != std::string::npos, "late subscriber received the latched sample");
        cmd(pub, "LIST");
        pub.close(); sub.close();
    }

    printf("\n[S8] same scenario WITHOUT a profile: late subscriber gets nothing (volatile) — proves S7 is the profile's doing\n");
    {
        auto c = baseCfg("s8pub");
        DdsTypedDriver pub(c); CHECK(pub.open(), "publisher open()");
        cmd(pub, std::string("PUBLISH ") + T + " id=88,label=volatile,speed=1");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        auto c2 = baseCfg("s8sub");
        DdsTypedDriver sub(c2); CHECK(sub.open(), "subscriber open()");
        cmd(sub, std::string("SUBSCRIBE ") + T);
        uint8_t buf[512] = {};
        auto rr = sub.receive(1500, std::span<uint8_t>(buf, sizeof buf), {}, std::string_view(""), std::stop_token{});
        CHECK(rr.bytes_read == 0, "no sample delivered to the late subscriber");
        pub.close(); sub.close();
    }

    printf("\n[S9] re-open with a different profile in the same process takes effect (domain lifetime fix)\n");
    {
        // pp (raw observer) keeps domain 42 alive here, so this only checks that profile changes
        // are per-driver, not per-domain — which is what matters for QoS.
        drain(pubs);
        auto c = baseCfg("s9"); c.qosProfileFile = P("USER_QOS_PROFILES_Cyclone.xml"); c.qosDefaultProfile = "PeriodicPattern";
        DdsTypedDriver d(c); CHECK(d.open(), "open()");
        cmd(d, std::string("PUBLISH ") + T + " id=1,label=a,speed=1"); settle();
        auto e = lookup(pubs, T, true);
        CHECK(e.rel == DDS_RELIABILITY_BEST_EFFORT, "PeriodicPattern -> BEST_EFFORT");
        d.close();
    }

    printf("\n==== %s (%d failure(s)) ====\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    dds_delete(pp);
    return g_fail ? 1 : 0;
}
