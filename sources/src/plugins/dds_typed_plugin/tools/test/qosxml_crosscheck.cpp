// Build: g++ -std=c++20 -I<dds_typed/src> <your -I flags> qosxml_crosscheck.cpp -lddsc -ldl -lpthread   (needs a Cyclone WITH the QoS Provider)
// Run:   ./a.out /path/USER_QOS_PROFILES_Cyclone.xml   -> compares provider vs built-in reader, profile by profile
// Cross-validation: Cyclone's own QoS Provider vs. the built-in DDS-XML reader, on the real file.
#include "dds_typed_driver.cpp"

#include <cstdio>

static void show(const char *tag, const dds_qos_t *q)
{
    if (!q) {
        printf("      %-8s (none)\n", tag);
        return;
    }
    dds_reliability_kind_t rk;
    dds_duration_t mb;
    dds_durability_kind_t dk;
    dds_history_kind_t hk;
    int32_t hd;
    dds_destination_order_kind_t dok;
    dds_duration_t lb = 0, ls = 0;
    bool ad = true;
    printf("      %-8s", tag);
    if (dds_qget_durability(q, &dk)) {
        printf(" dur=%d", (int)dk);
    }
    if (dds_qget_reliability(q, &rk, &mb)) {
        printf(" rel=%d/mbt=%lld", (int)rk, (long long)mb);
    }
    if (dds_qget_history(q, &hk, &hd)) {
        printf(" hist=%d/%d", (int)hk, hd);
    }
    if (dds_qget_destination_order(q, &dok)) {
        printf(" dord=%d", (int)dok);
    }
    if (dds_qget_latency_budget(q, &lb)) {
        printf(" lb=%lld", (long long)lb);
    }
    if (dds_qget_lifespan(q, &ls)) {
        printf(" ls=%lld", (long long)ls);
    }
    if (dds_qget_writer_data_lifecycle(q, &ad)) {
        printf(" autodispose=%d", (int)ad);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    const char *file = argv[1];
    qosxml::Document doc;
    std::string err;
    if (!qosxml::loadFile(file, doc, err)) {
        printf("builtin load failed: %s\n", err.c_str());
        return 2;
    }
    dds_qos_provider_t *whole = nullptr;
    int provOk                = (dds_create_qos_provider(file, &whole) == DDS_RETCODE_OK);
    printf("file=%s  builtin profiles=%zu  cyclone provider accepts file=%d\n", file, doc.profiles.size(), provOk);
    int mism = 0, compared = 0;
    for (const auto &[key, node] : doc.profiles) {
        printf("  %s\n", key.c_str());
        dds_qos_t *br = nullptr, *bw = nullptr, *bt = nullptr;
        std::vector<std::string> warn;
        std::string e2;
        bool ok = qosxml::buildProfile(*node, warn, br, bw, bt, e2);
        for (auto &w : warn) {
            printf("      WARN %s\n", w.c_str());
        }
        if (!ok) {
            printf("      builtin: cannot build: %s\n", e2.c_str());
            continue;
        }
        if (!provOk) {
            show("reader", br);
            show("writer", bw);
            show("topic", bt);
            continue;
        }
        dds_qos_provider_t *prov = nullptr;
        if (dds_create_qos_provider_scope(file, &prov, key.c_str()) != DDS_RETCODE_OK) {
            printf("      provider: scope failed\n");
            continue;
        }

        struct {
                dds_qos_kind_t k;
                const char *n;
                dds_qos_t *b;
        } kinds[] = {{DDS_READER_QOS, "reader", br}, {DDS_WRITER_QOS, "writer", bw}, {DDS_TOPIC_QOS, "topic", bt}};

        for (auto &kk : kinds) {
            const dds_qos_t *pq = nullptr;
            bool has            = dds_qos_provider_get_qos(prov, kk.k, key.c_str(), &pq) == DDS_RETCODE_OK && pq;
            bool same           = (has == (kk.b != nullptr)) && (!has || dds_qos_equal(pq, kk.b));
            ++compared;
            if (!same) {
                ++mism;
            }
            printf("      %-7s provider=%s builtin=%s  %s\n", kk.n, has ? "yes" : "no ", kk.b ? "yes" : "no ", same ? "IDENTICAL" : "*** DIFFERENT ***");
            if (!same) {
                show("provider", pq);
                show("builtin", kk.b);
            }
        }
        dds_delete_qos_provider(prov);
    }
    printf("\ncompared %d sections, %d mismatch(es)\n", compared, mism);
    return mism ? 1 : 0;
}
