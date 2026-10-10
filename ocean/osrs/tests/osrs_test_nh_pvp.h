#define OSRS_TEST_NH_PVP_REWARD_FLOAT_ITEM(key_name, field, default_value) \
    {.key = key_name, .value = default_value},
#define OSRS_TEST_NH_PVP_REWARD_INT_ITEM(key_name, field, default_value) \
    {.key = key_name, .value = default_value},
#define OSRS_TEST_NH_PVP_REWARD_ITEMS \
    OSRS_PVP_REWARD_FLOAT_FIELDS(OSRS_TEST_NH_PVP_REWARD_FLOAT_ITEM) \
    OSRS_PVP_REWARD_INT_FIELDS(OSRS_TEST_NH_PVP_REWARD_INT_ITEM)
