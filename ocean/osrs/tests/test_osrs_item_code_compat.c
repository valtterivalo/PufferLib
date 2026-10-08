#include "../osrs_inventory_clicks.h"
#include "osrs_test_check.h"

int main(void) {
    static const uint16_t existing_raw_ids[212] = {
        0, 10828, 21795, 1712, 2503, 4091, 1079, 4093, 4151, 9185, 4710, 5698,
        12954, 12829, 7462, 3105, 6737, 9243, 22324, 24417, 11791, 21006, 24424, 22647,
        11785, 26374, 13652, 11802, 26233, 4153, 21003, 11235, 19481, 22613, 27690, 22622,
        22636, 21018, 21021, 21024, 4712, 4714, 4736, 11834, 12831, 6585, 12002, 21295,
        13235, 11770, 25975, 6889, 11212, 4751, 4722, 4759, 4745, 4716, 4753, 4724,
        21932, 21791, 31113, 27251, 31106, 31097, 20657, 20997, 27235, 27238, 27241, 19547,
        28947, 26235, 12926, 4708, 19544, 22481, 6920, 20220, 2550, 23971, 22109, 23975,
        23979, 25865, 19921, 4089, 12899, 12612, 21326, 4097, 10382, 2497, 12788, 10499,
        4224, 13237, 22326, 22327, 22328, 11230, 22325, 24551, 26219, 28338, 26382, 26384,
        26386, 11832, 11836, 13239, 22981, 19553, 11773, 28307, 22322, 28310, 26241, 26243,
        26245, 28313, 27275, 11804, 11806, 11808, 23987, 1377, 21944, 21946, 892, 19484,
        12821, 12817, 11283, 30070, 28945, 12931, 24780, 10551, 29801, 30753, 30756, 27610,
        12006, 6685, 6687, 6689, 6691, 3024, 3026, 3028, 3030, 10925, 10927, 10929,
        10931, 12695, 12697, 12699, 12701, 23685, 23688, 23691, 23694, 2444, 169, 171,
        173, 23733, 23736, 23739, 23742, 30875, 30878, 30881, 30884, 4417, 4419, 4421,
        4423, 27641, 12913, 12915, 12917, 12919, 2434, 139, 141, 143, 22461, 22464,
        22467, 22470, 12625, 12627, 12629, 12631, 385, 3144, 27281, 4720, 4718, 24225,
        32352, 32336, 7218, 7220, 2313, 22081, 8013, 24621,
    };
    for (uint16_t code = 0; code < 212; code++) {
        uint16_t raw = existing_raw_ids[code];
        CHECK("existing raw item content codes remain stable", osrs_inventory_content_code_from_raw_osrs_id(raw) == code);
        CHECK("existing content codes retain raw item identity", osrs_item_content_metadata(code)->raw_osrs_id == raw);
    }
    for (int item = 0; item < NUM_ITEMS; item++) {
        uint16_t code = osrs_inventory_content_code_from_item(item);
        CHECK("gear content retains canonical item index", osrs_item_content_metadata(code)->item_idx == item);
        if (item >= 147) CHECK("new gear content appends after existing codes", code >= 212);
        CHECK("new gear round trips through raw identity", osrs_inventory_content_code_from_raw_osrs_id(ITEM_DATABASE[item].item_id) == code);
    }
    return osrs_test_summary();
}
