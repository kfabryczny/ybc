/*
 * Functional tests.
 */

#include "../platform.h"
#include "../ybc.h"

/* Since tests rely on assert(), NDEBUG must be undefined. */
#undef NDEBUG

#include <assert.h>
#include <stdio.h>   /* printf, fopen, fclose, fwrite */
#include <stdlib.h>  /* free, rand */
#include <string.h>  /* memcmp, memcpy, memset */

#define M_ERROR(error_message)  assert(0 && (error_message))

static void test_anonymous_cache_create(struct ybc *const cache)
{
  /* Non-forced open must fail. */
  if (ybc_open(cache, NULL, 0)) {
    M_ERROR("anonymous cache shouldn't be opened without force");
  }

  /* Forced open must succeed. */
  if (!ybc_open(cache, NULL, 1)) {
    M_ERROR("cannot open anonymous cache with force");
  }
  ybc_close(cache);
}

static void m_open_anonymous(struct ybc *const cache)
{
  if (!ybc_open(cache, NULL, 1)) {
    M_ERROR("cannot open anonymous cache");
  }
}

static void test_persistent_cache_create(struct ybc *const cache)
{
  char config_buf[ybc_config_get_size()];
  struct ybc_config *const config = (struct ybc_config *)config_buf;

  ybc_config_init(config);

  ybc_config_set_index_file(config, "./tmp_cache.index");
  ybc_config_set_data_file(config, "./tmp_cache.data");
  ybc_config_set_max_items_count(config, 1000);
  ybc_config_set_data_file_size(config, 1024 * 1024);

  /* Non-forced open must fail. */
  if (ybc_open(cache, config, 0)) {
    M_ERROR("non-existing persistent cache shouldn't be opened "
        "without force");
  }

  /* Forced open must succeed. */
  if (!ybc_open(cache, config, 1)) {
    M_ERROR("cannot create persistent cache");
  }
  ybc_close(cache);

  /* Non-forced open must succeed now. */
  if (!ybc_open(cache, config, 0)) {
    M_ERROR("cannot open existing persistent cache");
  }
  ybc_close(cache);

  /* Remove files associated with the cache. */
  ybc_remove(config);

  /* Non-forced open must fail again. */
  if (ybc_open(cache, config, 0)) {
    M_ERROR("non-existing persistent cache shouldn't be opened "
        "without force");
  }

  ybc_config_destroy(config);
}

static void expect_value(struct ybc_item *const item,
    const struct ybc_value *const expected_value)
{
  struct ybc_value actual_value;

  ybc_item_get_value(item, &actual_value);

  assert(actual_value.size == expected_value->size);
  assert(
      memcmp(actual_value.ptr, expected_value->ptr, actual_value.size) == 0);
  assert(actual_value.ttl <= expected_value->ttl);
}

static void expect_item_miss(struct ybc *const cache,
    const struct ybc_key *const key)
{
  char item_buf[ybc_item_get_size()];
  struct ybc_item *const item = (struct ybc_item *)item_buf;

  if (ybc_item_get(cache, item, key)) {
    M_ERROR("unexpected item found");
  }
}

static void expect_item_hit(struct ybc *const cache,
    const struct ybc_key *const key,
    const struct ybc_value *const expected_value)
{
  char item_buf[ybc_item_get_size()];
  struct ybc_item *const item = (struct ybc_item *)item_buf;

  if (!ybc_item_get(cache, item, key)) {
    M_ERROR("cannot find expected item");
  }
  expect_value(item, expected_value);
  ybc_item_release(item);
}

static void expect_item_set(struct ybc *const cache,
    const struct ybc_key *const key, const struct ybc_value *const value)
{
  char item_buf[ybc_item_get_size()];
  struct ybc_item *const item = (struct ybc_item *)item_buf;

  if (!ybc_item_set_item(cache, item, key, value)) {
    M_ERROR("error when storing item in the cache");
  }
  expect_value(item, value);
  ybc_item_release(item);
  expect_item_hit(cache, key, value);
}

static void expect_item_set_no_acquire(struct ybc *const cache,
    const struct ybc_key *const key, const struct ybc_value *const value)
{
  if (!ybc_item_set(cache, key, value)) {
    M_ERROR("error when storing item in the cache");
  }
  expect_item_hit(cache, key, value);
}

static void expect_item_remove(struct ybc *const cache,
    struct ybc_key *const key)
{
  if (!ybc_item_remove(cache, key)) {
    M_ERROR("error when deleting item from the cache");
  }
  expect_item_miss(cache, key);
  if (ybc_item_remove(cache, key)) {
    M_ERROR("unexpected item found in the cache");
  }
}

static void expect_item_miss_de(struct ybc *const cache,
    const struct ybc_key *const key, const size_t grace_ttl)
{
  char item_buf[ybc_item_get_size()];
  struct ybc_item *const item = (struct ybc_item *)item_buf;

  if (ybc_item_get_de(cache, item, key, grace_ttl) == YBC_DE_SUCCESS) {
    M_ERROR("unexpected item found");
  }
}

static void expect_item_hit_de(struct ybc *const cache,
    const struct ybc_key *const key,
    const struct ybc_value *const expected_value, const size_t grace_ttl)
{
  char item_buf[ybc_item_get_size()];
  struct ybc_item *const item = (struct ybc_item *)item_buf;

  if (ybc_item_get_de(cache, item, key, grace_ttl) != YBC_DE_SUCCESS) {
    M_ERROR("cannot find expected item");
  }
  expect_value(item, expected_value);
  ybc_item_release(item);
}

static void test_set_txn_rollback(struct ybc *const cache,
    struct ybc_set_txn *const txn, const struct ybc_key *const key,
    const size_t value_size)
{
  if (!ybc_set_txn_begin(cache, txn, key, value_size, YBC_MAX_TTL)) {
    M_ERROR("error when starting set transaction");
  }

  expect_item_miss(cache, key);

  ybc_set_txn_rollback(txn);
}

static void test_set_txn_commit_item(struct ybc *const cache,
    struct ybc_set_txn *const txn, const struct ybc_key *const key,
    const struct ybc_value *const value)
{
  if (!ybc_set_txn_begin(cache, txn, key, value->size, value->ttl)) {
    M_ERROR("error when starting set transaction");
  }

  struct ybc_set_txn_value txn_value;
  ybc_set_txn_get_value(txn, &txn_value);
  assert(txn_value.ptr != NULL);
  assert(txn_value.size == value->size);
  memcpy(txn_value.ptr, value->ptr, value->size);

  char item_buf[ybc_item_get_size()];
  struct ybc_item *const item = (struct ybc_item *)item_buf;

  ybc_set_txn_commit_item(txn, item);

  expect_value(item, value);
  ybc_item_release(item);

  expect_item_hit(cache, key, value);
}

static void test_set_txn_commit(struct ybc *const cache,
    struct ybc_set_txn *const txn, const struct ybc_key *const key,
    const struct ybc_value *const value)
{
  if (!ybc_set_txn_begin(cache, txn, key, value->size, value->ttl)) {
    M_ERROR("error when starting set transaction");
  }

  struct ybc_set_txn_value txn_value;
  ybc_set_txn_get_value(txn, &txn_value);
  assert(txn_value.ptr != NULL);
  assert(txn_value.size == value->size);
  memcpy(txn_value.ptr, value->ptr, value->size);

  ybc_set_txn_commit(txn);

  expect_item_hit(cache, key, value);
}

static void test_set_txn_update_value_size(struct ybc *const cache,
    struct ybc_set_txn *const txn, const struct ybc_key *const key,
    const struct ybc_value *const value)
{
  if (!ybc_set_txn_begin(cache, txn, key, value->size + 10, value->ttl)) {
    M_ERROR("error when starting set transaction");
  }

  struct ybc_set_txn_value txn_value;
  ybc_set_txn_get_value(txn, &txn_value);
  assert(txn_value.ptr != NULL);
  assert(txn_value.size == value->size + 10);
  memcpy(txn_value.ptr, value->ptr, value->size);

  ybc_set_txn_update_value_size(txn, value->size);
  ybc_set_txn_commit(txn);

  expect_item_hit(cache, key, value);
}

static void test_set_txn_commit_all(struct ybc *const cache,
    struct ybc_set_txn *const txn, const struct ybc_key *const key,
    const struct ybc_value *const value)
{
  test_set_txn_commit(cache, txn, key, value);
  test_set_txn_commit_item(cache, txn, key, value);
  test_set_txn_update_value_size(cache, txn, key, value);
}

static void test_set_txn_failure(struct ybc *const cache,
    struct ybc_set_txn *const txn, const struct ybc_key *const key,
    const size_t value_size)
{
  if (ybc_set_txn_begin(cache, txn, key, value_size, YBC_MAX_TTL)) {
    M_ERROR("unexpected transaction success");
  }
}

static void test_set_txn_ops(struct ybc *const cache)
{
  m_open_anonymous(cache);

  char set_txn_buf[ybc_set_txn_get_size()];
  struct ybc_set_txn *const txn = (struct ybc_set_txn *)set_txn_buf;

  struct ybc_key key = {
      .ptr = "abc",
      .size = 3,
  };

  struct ybc_value value = {
      .ptr = "qwerty",
      .size = 6,
      .ttl = YBC_MAX_TTL,
  };

  test_set_txn_rollback(cache, txn, &key, value.size);

  test_set_txn_commit_all(cache, txn, &key, &value);

  /* Test zero-length key. */
  key.size = 0;
  test_set_txn_commit_all(cache, txn, &key, &value);

  /* Test zero-length value. */
  value.size = 0;
  test_set_txn_commit_all(cache, txn, &key, &value);

  /* Test too large key. */
  value.size = 6;
  key.size = SIZE_MAX;
  test_set_txn_failure(cache, txn, &key, value.size);

  /* Test too large value. */
  key.size = 3;
  test_set_txn_failure(cache, txn, &key, SIZE_MAX);
  test_set_txn_failure(cache, txn, &key, SIZE_MAX / 2);

  ybc_close(cache);
}

static void test_item_ops(struct ybc *const cache,
    const size_t iterations_count)
{
  m_open_anonymous(cache);

  struct ybc_key key;
  struct ybc_value value;

  value.ttl = YBC_MAX_TTL;

  for (size_t i = 0; i < iterations_count; ++i) {
    key.ptr = &i;
    key.size = sizeof(i);

    expect_item_miss(cache, &key);
  }

  for (size_t i = 0; i < iterations_count; ++i) {
    key.ptr = &i;
    key.size = sizeof(i);
    value.ptr = &i;
    value.size = sizeof(i);

    expect_item_set_no_acquire(cache, &key, &value);
    expect_item_set(cache, &key, &value);
    expect_item_remove(cache, &key);
  }

  for (size_t i = 0; i < iterations_count; ++i) {
    key.ptr = &i;
    key.size = sizeof(i);

    expect_item_miss(cache, &key);
  }

  ybc_close(cache);
}

static void test_expiration(struct ybc *const cache)
{
  m_open_anonymous(cache);

  const struct ybc_key key = {
      .ptr = "aaa",
      .size = 3,
  };
  const struct ybc_value value = {
      .ptr = "1234",
      .size = 4,
      .ttl = 200,
  };
  expect_item_set(cache, &key, &value);

  p_sleep(300);

  /* The item should expire now. */
  expect_item_miss(cache, &key);

  ybc_close(cache);
}

static void test_dogpile_effect_ops(struct ybc *const cache)
{
  m_open_anonymous(cache);

  struct ybc_key key = {
      .ptr = "foo",
      .size = 3,
  };
  const struct ybc_value value = {
      .ptr = "bar",
      .size = 3,
      .ttl = 2 * 1000,
  };

  /*
   * De-aware method should return an empty item on the first try
   * for non-existing item. The second try for the same non-existing item
   * will result in waiting for up to grace ttl period of time.
   */
  expect_item_miss_de(cache, &key, 200);

  /* Will wait for 200 milliseconds. */
  expect_item_miss_de(cache, &key, 10 * 1000);

  key.ptr = "bar";
  expect_item_set(cache, &key, &value);

  /*
   * If grace ttl is smaller than item's ttl, then the item should be returned.
   */
  expect_item_hit_de(cache, &key, &value, value.ttl / 10);

  /*
   * If grace ttl is larger than item's ttl, then an empty item
   * should be returned on the first try and the item itself should be returned
   * on subsequent tries irregardless of grace ttl value.
   */
  expect_item_miss_de(cache, &key, value.ttl * 10);
  expect_item_hit_de(cache, &key, &value, value.ttl * 10);
  expect_item_hit_de(cache, &key, &value, value.ttl / 10);

  ybc_close(cache);
}

static void test_dogpile_effect_ops_async(struct ybc *const cache)
{
  char item_buf[ybc_item_get_size()];
  struct ybc_item *const item = (struct ybc_item *)item_buf;

  m_open_anonymous(cache);

  struct ybc_key key = {
      .ptr = "foo",
      .size = 3,
  };
  const struct ybc_value value = {
      .ptr = "bar",
      .size = 3,
      .ttl = 2 * 1000,
  };

  /*
   * De-aware method should return an empty item on the first try
   * for non-existing item. The second try for the same non-existing item
   * should result in YBC_DE_WOULDBLOCK.
   */
  if (ybc_item_get_de_async(cache, item, &key, 10 * 1000) != YBC_DE_NOTFOUND) {
    M_ERROR("unexpected status returned from ybc_item_get_de_async()");
  }

  /* Should return immediately instead of waiting for 10 seconds. */
  if (ybc_item_get_de_async(cache, item, &key, 5 * 1000) !=
      YBC_DE_WOULDBLOCK) {
    M_ERROR("unexpected status returned from ybc_item_get_de_async()");
  }

  key.ptr = "bar";
  expect_item_set(cache, &key, &value);

  if (ybc_item_get_de_async(cache, item, &key, value.ttl / 10) !=
      YBC_DE_SUCCESS) {
    M_ERROR("unexpected status returned from ybc_item_get_de_async()");
  }
  ybc_item_release(item);

  /*
   * If grace ttl is larger than item's ttl, then an empty item
   * should be returned on the first try and the item itself should be returned
   * on subsequent tries irregardless of grace ttl value.
   */
  if (ybc_item_get_de_async(cache, item, &key, value.ttl * 10) !=
      YBC_DE_NOTFOUND) {
    M_ERROR("unexpected status returned from ybc_item_get_de_async()");
  }

  if (ybc_item_get_de_async(cache, item, &key, value.ttl * 10) !=
      YBC_DE_SUCCESS) {
    M_ERROR("unexpected status returned from ybc_item_get_de_async()");
  }
  ybc_item_release(item);

  if (ybc_item_get_de_async(cache, item, &key, value.ttl / 10) !=
      YBC_DE_SUCCESS) {
    M_ERROR("unexpected status returned from ybc_item_get_de_async()");
  }
  ybc_item_release(item);

  ybc_close(cache);
}

static void m_test_de_hashtable(struct ybc *const cache,
    const size_t hashtable_size, const size_t pending_items_count)
{
  char config_buf[ybc_config_get_size()];
  struct ybc_config *const config = (struct ybc_config *)config_buf;

  ybc_config_init(config);

  ybc_config_set_de_hashtable_size(config, hashtable_size);

  if (!ybc_open(cache, config, 1)) {
    M_ERROR("cannot create an anonymous cache");
  }

  ybc_config_destroy(config);

  char item_buf[ybc_item_get_size()];
  struct ybc_item *const item = (struct ybc_item *)item_buf;

  size_t i;
  struct ybc_key key = {
      .ptr = &i,
      .size = sizeof(i),
  };

  for (i = 0; i < pending_items_count; ++i) {
    if (ybc_item_get_de_async(cache, item, &key, 1000) != YBC_DE_NOTFOUND) {
      M_ERROR("unexpected status returned from ybc_item_get_de_async()");
    }

    if (ybc_item_get_de_async(cache, item, &key, 1000) != YBC_DE_WOULDBLOCK) {
      M_ERROR("unexpected status returned from ybc_item_get_de_async()");
    }
  }

  ybc_close(cache);
}

static void test_dogpile_effect_hashtable(struct ybc *const cache)
{
  for (size_t hashtable_size = 1; hashtable_size <= 1000;
      hashtable_size *= 10) {
    for (size_t pending_items_count = 1; pending_items_count <= 10000;
        pending_items_count *= 100) {
      m_test_de_hashtable(cache, hashtable_size, pending_items_count);
    }
  }
}

static void test_cluster_ops(const size_t cluster_size,
    const size_t iterations_count)
{
  char configs_buf[ybc_config_get_size() * cluster_size];
  struct ybc_config *const configs = (struct ybc_config *)configs_buf;

  for (size_t i = 0; i < cluster_size; ++i) {
    ybc_config_init(YBC_CONFIG_GET(configs, i));
  }

  char cluster_buf[ybc_cluster_get_size(cluster_size)];
  struct ybc_cluster *const cluster = (struct ybc_cluster *)cluster_buf;

  /* Unfored open must fail. */
  if (ybc_cluster_open(cluster, configs, cluster_size, 0)) {
    M_ERROR("cache cluster shouldn't be opened without force");
  }

  /* Forced open must succeed. */
  if (!ybc_cluster_open(cluster, configs, cluster_size, 1)) {
    M_ERROR("failed opening cache cluster");
  }

  /* Configs are no longer needed, so they can be destroyed. */
  for (size_t i = 0; i < cluster_size; ++i) {
    ybc_config_destroy(YBC_CONFIG_GET(configs, i));
  }

  struct ybc_key key;
  struct ybc_value value;

  value.ttl = YBC_MAX_TTL;

  for (size_t i = 0; i < iterations_count; ++i) {
    key.ptr = &i;
    key.size = sizeof(i);
    value.ptr = &i;
    value.size = sizeof(i);

    struct ybc *const cache = ybc_cluster_get_cache(cluster, &key);
    expect_item_set(cache, &key, &value);
  }

  ybc_cluster_clear(cluster);

  for (size_t i = 0; i < iterations_count; ++i) {
    key.ptr = &i;
    key.size = sizeof(i);
    value.ptr = &i;
    value.size = sizeof(i);

    struct ybc *const cache = ybc_cluster_get_cache(cluster, &key);
    expect_item_miss(cache, &key);
  }

  ybc_cluster_close(cluster);
}

static void test_simple_ops(struct ybc *const cache)
{
  m_open_anonymous(cache);

  struct ybc_key key;
  struct ybc_value value;

  size_t i = 0;
  key.ptr = &i;
  key.size = sizeof(i);
  value.ptr = &i;
  value.size = sizeof(i);
  value.ttl = YBC_MAX_TTL;

  if (ybc_simple_get(cache, &key, &value) != 0) {
    M_ERROR("unexpected result returned from ybc_simple_get()");
  }

  for (i = 0; i < 1000; i++) {
    if (!ybc_simple_set(cache, &key, &value)) {
      M_ERROR("unexpected error in ybc_simple_set()");
    }
  }

  value.size--;
  i--;
  if (ybc_simple_get(cache, &key, &value) != -1) {
    M_ERROR("unexpected result returned from ybc_simple_get()");
  }
  assert(value.size == sizeof(i));

  size_t j;
  value.ptr = &j;
  for (i = 0; i < 1000; i++) {
    int rv = ybc_simple_get(cache, &key, &value);
    if (rv == 0) {
      continue;
    }
    assert(rv == 1);
    assert(j == i);
  }

  ybc_close(cache);
}

static struct ybc_item *m_get_item(struct ybc_item *const items, const size_t i)
{
  return (struct ybc_item *)(((char *)items) + ybc_item_get_size() * i);
}

static void test_overlapped_acquirements(struct ybc *const cache,
    const size_t items_count)
{
  m_open_anonymous(cache);

  char added_items_buf[ybc_item_get_size() * items_count];
  struct ybc_item *const added_items = (struct ybc_item *)added_items_buf;

  char obtained_items_buf[ybc_item_get_size() * items_count];
  struct ybc_item *const obtained_items = (struct ybc_item *)obtained_items_buf;

  size_t i;
  const struct ybc_key key = {
      .ptr = &i,
      .size = sizeof(i),
  };
  const struct ybc_value value = {
      .ptr = &i,
      .size = sizeof(i),
      .ttl = YBC_MAX_TTL,
  };

  const struct ybc_key static_key = {
      .ptr = "aaaabbb",
      .size = 7,
  };
  for (i = 0; i < items_count; ++i) {
    ybc_item_set_item(cache, m_get_item(added_items, i), &static_key, &value);
  }
  for (i = 0; i < items_count; ++i) {
    ybc_item_get(cache, m_get_item(obtained_items, i), &static_key);
  }
  for (i = 0; i < items_count; ++i) {
    ybc_item_release(m_get_item(obtained_items, i));
    ybc_item_release(m_get_item(added_items, i));
  }

  for (i = 0; i < items_count; ++i) {
    ybc_item_set_item(cache, m_get_item(added_items, i), &key, &value);
  }

  for (i = 0; i < items_count; ++i) {
    ybc_item_get(cache, m_get_item(obtained_items, i), &key);
    expect_value(m_get_item(obtained_items, i), &value);
  }

  for (i = 0; i < items_count; ++i) {
    ybc_item_release(m_get_item(obtained_items, items_count - i - 1));
  }

  for (i = 0; i < items_count; ++i) {
    ybc_item_release(m_get_item(added_items, i));
  }

  ybc_close(cache);
}

static void test_interleaved_sets(struct ybc *const cache)
{
  m_open_anonymous(cache);

  char set_txn1_buf[ybc_set_txn_get_size()];
  char set_txn2_buf[ybc_set_txn_get_size()];
  struct ybc_set_txn *const txn1 = (struct ybc_set_txn *)set_txn1_buf;
  struct ybc_set_txn *const txn2 = (struct ybc_set_txn *)set_txn2_buf;

  char item1_buf[ybc_item_get_size()];
  char item2_buf[ybc_item_get_size()];
  struct ybc_item *const item1 = (struct ybc_item *)item1_buf;
  struct ybc_item *const item2 = (struct ybc_item *)item2_buf;

  const struct ybc_key key1 = {
      .ptr = "foo",
      .size = 3,
  };
  const struct ybc_key key2 = {
      .ptr = "barz",
      .size = 4,
  };

  const struct ybc_value value1 = {
      .ptr = "123456",
      .size = 6,
      .ttl = YBC_MAX_TTL,
  };
  const struct ybc_value value2 = {
      .ptr = "qwert",
      .size = 4,
      .ttl = YBC_MAX_TTL,
  };

  if (!ybc_set_txn_begin(cache, txn1, &key1, value1.size, value1.ttl)) {
    M_ERROR("Cannot start the first set transaction");
  }

  if (!ybc_set_txn_begin(cache, txn2, &key2, value2.size, value2.ttl)) {
    M_ERROR("Cannot start the second set transaction");
  }

  struct ybc_set_txn_value txn_value;

  ybc_set_txn_get_value(txn1, &txn_value);
  assert(txn_value.size == value1.size);
  memcpy(txn_value.ptr, value1.ptr, value1.size);

  ybc_set_txn_get_value(txn2, &txn_value);
  assert(txn_value.size == value2.size);
  memcpy(txn_value.ptr, value2.ptr, value2.size);

  expect_item_miss(cache, &key1);
  expect_item_miss(cache, &key2);

  ybc_set_txn_commit_item(txn1, item1);
  ybc_set_txn_commit_item(txn2, item2);

  expect_value(item1, &value1);
  expect_value(item2, &value2);

  ybc_item_release(item1);
  ybc_item_release(item2);

  expect_item_hit(cache, &key1, &value1);
  expect_item_hit(cache, &key2, &value2);

  ybc_close(cache);
}

static void test_instant_clear(struct ybc *const cache)
{
  char config_buf[ybc_config_get_size()];
  struct ybc_config *const config = (struct ybc_config *)config_buf;

  ybc_config_init(config);

  ybc_config_set_max_items_count(config, 1000);
  ybc_config_set_data_file_size(config, 128 * 1024);

  if (!ybc_open(cache, config, 1)) {
    M_ERROR("cannot create anonymous cache");
  }

  ybc_config_destroy(config);

  struct ybc_key key;
  struct ybc_value value;

  /* Add a lot of items to the cache */
  value.ttl = YBC_MAX_TTL;
  for (size_t i = 0; i < 1000; ++i) {
    key.ptr = &i;
    key.size = sizeof(i);
    value.ptr = &i;
    value.size = sizeof(i);
    expect_item_set(cache, &key, &value);
  }

  ybc_clear(cache);

  /* Test that the cache doesn't contain any items after the clearance. */
  for (size_t i = 0; i < 1000; ++i) {
    key.ptr = &i;
    key.size = sizeof(i);
    expect_item_miss(cache, &key);
  }

  ybc_close(cache);
}

static void expect_persistent_survival(struct ybc *const cache,
    const uint64_t sync_interval)
{
  char config_buf[ybc_config_get_size()];
  struct ybc_config *const config = (struct ybc_config *)config_buf;

  ybc_config_init(config);

  ybc_config_set_index_file(config, "./tmp_cache.index");
  ybc_config_set_data_file(config, "./tmp_cache.data");
  ybc_config_set_max_items_count(config, 10);
  ybc_config_set_data_file_size(config, 1024);
  ybc_config_set_sync_interval(config, sync_interval);

  if (!ybc_open(cache, config, 1)) {
    M_ERROR("cannot create persistent cache");
  }

  const struct ybc_key key = {
      .ptr = "foobar",
      .size = 6.
  };
  const struct ybc_value value = {
      .ptr = "qwert",
      .size = 5,
      .ttl = YBC_MAX_TTL,
  };
  expect_item_set(cache, &key, &value);

  ybc_close(cache);

  /* Re-open the same cache and make sure the item exists there. */

  if (!ybc_open(cache, config, 0)) {
    M_ERROR("cannot open persistent cache");
  }

  expect_item_hit(cache, &key, &value);

  ybc_close(cache);

  ybc_remove(config);

  ybc_config_destroy(config);
}

static void test_persistent_survival(struct ybc *const cache)
{
  /*
   * Test persistence with enabled data syncing.
   */
  expect_persistent_survival(cache, 10 * 1000);

  /*
   * Test persistence with disabled data syncing.
   */
  expect_persistent_survival(cache, 0);
}

static void test_broken_index_handling(struct ybc *const cache)
{
  char config_buf[ybc_config_get_size()];
  struct ybc_config *const config = (struct ybc_config *)config_buf;

  ybc_config_init(config);

  ybc_config_set_index_file(config, "./tmp_cache.index");
  ybc_config_set_data_file(config, "./tmp_cache.data");
  ybc_config_set_max_items_count(config, 1000);
  ybc_config_set_data_file_size(config, 64 * 1024);

  /* Create index and data files. */
  if (!ybc_open(cache, config, 1)) {
    M_ERROR("cannot create persistent cache");
  }

  struct ybc_key key;
  struct ybc_value value = {
      .ptr = "foobar",
      .size = 6,
      .ttl = YBC_MAX_TTL,
  };

  /* Add some data to cache. */
  for (size_t i = 0; i < 1000; i++) {
    key.ptr = &i;
    key.size = sizeof(i);
    expect_item_set(cache, &key, &value);
  }

  ybc_close(cache);

  /* Corrupt index file. */
  FILE *const fp = fopen("./tmp_cache.index", "r+");
  int rv = fseek(fp, 0, SEEK_END);
  if (rv != 0) {
    M_ERROR("fseek(SEEK_END, 0) failed");
  }
  const long pos = ftell(fp);
  if (pos < 0) {
    M_ERROR("ftell failed");
  }
  const size_t file_size = (size_t)pos;
  rv = fseek(fp, 0, SEEK_SET);
  if (rv != 0) {
    M_ERROR("fseek(SEEK_SET, 0) failed");
  }
  for (size_t i = 0; i < file_size; ++i) {
    if (fputc((unsigned char)i, fp) == EOF) {
      M_ERROR("cannot write data");
    }
  }
  fclose(fp);

  /* Try reading index file. It must become "empty". */
  if (!ybc_open(cache, config, 0)) {
    M_ERROR("cannot open persistent cache");
  }

  for (size_t i = 0; i < 1000; ++i) {
    key.ptr = &i;
    key.size = sizeof(i);
    expect_item_miss(cache, &key);
  }

  ybc_close(cache);

  /* Remove index and data files. */
  ybc_remove(config);

  ybc_config_destroy(config);
}

static void provoke_data_wrapping(struct ybc *const cache)
{
  const size_t value_buf_size = 13 * 3457;
  char *const value_buf = p_malloc(value_buf_size);
  memset(value_buf, 'q', value_buf_size);

  struct ybc_key key;
  const struct ybc_value value = {
    .ptr = value_buf,
    .size = value_buf_size,
    .ttl = YBC_MAX_TTL,
  };

  /* Test handling of cache data size wrapping. */
  for (size_t i = 0; i < 10 * 1000; ++i) {
    key.ptr = &i;
    key.size = sizeof(i);
    expect_item_set(cache, &key, &value);
  }

  p_free(value_buf);
}

static void test_large_cache(struct ybc *const cache)
{
  char config_buf[ybc_config_get_size()];
  struct ybc_config *const config = (struct ybc_config *)config_buf;

  ybc_config_init(config);

  ybc_config_set_max_items_count(config, 10 * 1000);
  ybc_config_set_data_file_size(config, 32 * 1024 * 1024);

  if (!ybc_open(cache, config, 1)) {
    M_ERROR("cannot create anonymous cache");
  }

  ybc_config_destroy(config);

  provoke_data_wrapping(cache);

  ybc_close(cache);
}

static void test_overwrite_protection(struct ybc *const cache)
{
  char config_buf[ybc_config_get_size()];
  struct ybc_config *const config = (struct ybc_config*)config_buf;

  ybc_config_init(config);

  ybc_config_set_max_items_count(config, 10 * 1000);
  ybc_config_set_data_file_size(config, 1024 * 1024);

  if (!ybc_open(cache, config, 1)) {
    M_ERROR("cannot create anonymous cache");
  }

  ybc_config_destroy(config);

  const struct ybc_key survive_key = {
    .ptr = "you_should_survive :)",
    .size = 21,
  };
  const struct ybc_value survive_value = {
    .ptr = "survive, please!",
    .size = 16,
    .ttl = YBC_MAX_TTL,
  };

  provoke_data_wrapping(cache);

  char survive_item_buf[ybc_item_get_size()];
  struct ybc_item *const survive_item = (struct ybc_item *)survive_item_buf;
  ybc_item_set_item(cache, survive_item, &survive_key, &survive_value);
  expect_value(survive_item, &survive_value);

  provoke_data_wrapping(cache);

  expect_value(survive_item, &survive_value);
  ybc_item_release(survive_item);

  ybc_close(cache);
}

static void test_out_of_memory(struct ybc *const cache)
{
  char config_buf[ybc_config_get_size()];
  struct ybc_config *const config = (struct ybc_config *)config_buf;

  ybc_config_init(config);

  ybc_config_set_data_file_size(config, 1024 * 1024);

  if (!ybc_open(cache, config, 1)) {
    M_ERROR("cannot create anonymous cache");
  }

  ybc_config_destroy(config);

  const size_t value_buf_size = 1024 * 1024 + 1;
  void *const value_buf = p_malloc(value_buf_size);
  memset(value_buf, 0, value_buf_size);

  char item_buf[ybc_item_get_size()];
  struct ybc_item *const item = (struct ybc_item *)item_buf;

  struct ybc_key key = {
    .ptr = "foobar",
    .size = 6,
  };
  struct ybc_value value = {
    .ptr = value_buf,
    .size = value_buf_size,
    .ttl = YBC_MAX_TTL,
  };

  /*
   * The value size exceeds cache size.
   */
  if (ybc_item_set(cache, &key, &value)) {
    M_ERROR("unexpected item addition");
  }

  /*
   * The acquired item should prevent from adding new item into the cache.
   */
  value.size -= 1000;
  if (!ybc_item_set_item(cache, item, &key, &value)) {
    M_ERROR("cannot store item to cache");
  }

  char item2_buf[ybc_item_get_size()];
  struct ybc_item *const item2 =(struct ybc_item *)item2_buf;

  key.ptr = "abcdef";
  value.size = 1000;
  if (ybc_item_set_item(cache, item2, &key, &value)) {
    M_ERROR("unexpected item addition");
  }

  ybc_item_release(item);

  /*
   * Now the item2 should be added, since the item is released
   * and the cache has enough room for the item2.
   */
  if (!ybc_item_set_item(cache, item2, &key, &value)) {
    M_ERROR("cannot store item to cache");
  }
  ybc_item_release(item2);

  p_free(value_buf);

  ybc_close(cache);
}

static int is_item_exists(struct ybc *const cache,
    const struct ybc_key *const key)
{
  char item_buf[ybc_item_get_size()];
  struct ybc_item *const item = (struct ybc_item *)item_buf;

  if (!ybc_item_get(cache, item, key)) {
    return 0;
  }
  ybc_item_release(item);
  return 1;
}

static void expect_cache_with_data(struct ybc *const cache,
    const size_t items_count, const size_t expected_hits_count,
    struct ybc_key *const key, struct ybc_value *const value)
{
  size_t hits_count = 0;
  for (size_t i = 0; i < items_count; ++i) {
    key->ptr = &i;
    key->size = sizeof(i);
    if (is_item_exists(cache, key)) {
      value->ptr = &i;
      value->size = sizeof(i);
      expect_item_hit(cache, key, value);
      ++hits_count;
    }
  }

  assert(hits_count > expected_hits_count);
}

static void expect_cache_works(struct ybc *const cache,
    const size_t items_count, const size_t expected_hits_count,
    const size_t hot_items_count, const size_t hot_data_size,
    const uint64_t sync_interval)
{
  char config_buf[ybc_config_get_size()];
  struct ybc_config *const config = (struct ybc_config *)config_buf;

  ybc_config_init(config);

  assert(items_count <= SIZE_MAX / 100);
  ybc_config_set_max_items_count(config, items_count * 2);
  ybc_config_set_data_file_size(config, items_count * 100);
  ybc_config_set_hot_items_count(config, hot_items_count);
  ybc_config_set_hot_data_size(config, hot_data_size);
  ybc_config_set_sync_interval(config, sync_interval);

  if (!ybc_open(cache, config, 1)) {
    M_ERROR("cannot create anonymous cache");
  }

  ybc_config_destroy(config);

  struct ybc_key key;
  struct ybc_value value;

  value.ttl = YBC_MAX_TTL;

  for (size_t i = 0; i < items_count; ++i) {
    key.ptr = &i;
    key.size = sizeof(i);
    value.ptr = &i;
    value.size = sizeof(i);
    expect_item_set(cache, &key, &value);
  }

  /*
   * Verify twice that the cache contains added data.
   * The second verification checks correctness of internal cache algorithms,
   * which may re-arrange data when reading it during the first check
   * (for instance, cache compaction algorithms).
   */
  expect_cache_with_data(cache, items_count, expected_hits_count, &key, &value);
  expect_cache_with_data(cache, items_count, expected_hits_count, &key, &value);

  ybc_close(cache);
}

static void test_data_compaction(struct ybc *const cache)
{
  /*
   * The cache will compact data on items' retrieval,
   * because items_count * item_size is greater than hot_data_size.
   * It is assumed that item_size is equal to 2*sizeof(size_t)
   * (8 bytes on 32-bit builds and 16 bytes on 64-bit builds).
   * See expect_cache_works() sources for details.
   */

  const size_t items_count = 1000;
  const size_t expected_hits_count = 900;
  const size_t hot_items_count = 1000;
  const size_t hot_data_size = items_count * sizeof(size_t) * 3;
  const uint64_t sync_interval = 10 * 1000;

  expect_cache_works(cache, items_count, expected_hits_count, hot_items_count,
      hot_data_size, sync_interval);
}

static void test_small_sync_interval(struct ybc *const cache)
{
  char config_buf[ybc_config_get_size()];
  struct ybc_config *const config = (struct ybc_config *)config_buf;

  ybc_config_init(config);

  ybc_config_set_max_items_count(config, 100);
  ybc_config_set_data_file_size(config, 4000);
  ybc_config_set_sync_interval(config, 100);

  if (!ybc_open(cache, config, 1)) {
    M_ERROR("cannot create anonymous cache");
  }

  ybc_config_destroy(config);

  struct ybc_key key;
  const struct ybc_value value = {
      .ptr = "1234567890a",
      .size = 11,
      .ttl = YBC_MAX_TTL,
  };

  for (size_t i = 0; i < 10; ++i) {
    for (size_t j = 0; j < 100; ++j) {
      key.ptr = &j;
      key.size = sizeof(j);
      expect_item_set(cache, &key, &value);
    }
    p_sleep(31);
  }

  ybc_close(cache);
}

static void test_disabled_hot_items_cache(struct ybc *const cache)
{
  const size_t items_count = 1000;
  const size_t expected_hits_count = 900;
  const size_t hot_items_count = 0;
  const size_t hot_data_size = 100 * 1024;
  const uint64_t sync_interval = 10 * 1000;

  expect_cache_works(cache, items_count, expected_hits_count, hot_items_count,
      hot_data_size, sync_interval);
}

static void test_disabled_data_compaction(struct ybc *const cache)
{
  const size_t items_count = 1000;
  const size_t expected_hits_count = 900;
  const size_t hot_items_count = 100;
  const size_t hot_data_size = 0;
  const uint64_t sync_interval = 10 * 1000;

  expect_cache_works(cache, items_count, expected_hits_count, hot_items_count,
      hot_data_size, sync_interval);
}

static void test_disabled_syncing(struct ybc *const cache)
{
  const size_t items_count = 1000;
  const size_t expected_hits_count = 900;
  const size_t hot_items_count = 100;
  const size_t hot_data_size = 10 * 1024;
  const uint64_t sync_interval = 0;

  expect_cache_works(cache, items_count, expected_hits_count, hot_items_count,
      hot_data_size, sync_interval);
}

struct thread_task
{
  struct ybc *const cache;
  int should_exit;
};

static void thread_func(void *const ctx)
{
  struct thread_task *const task = ctx;

  char item_buf[ybc_item_get_size()];
  struct ybc_item *const item = (struct ybc_item *)item_buf;

  struct ybc_key key;
  struct ybc_value value;
  int tmp;

  key.size = sizeof(tmp);
  value.size = sizeof(tmp);
  value.ttl = YBC_MAX_TTL;

  while (!task->should_exit) {
    /*
     * It is OK using non-threadsafe rand() function here.
     */
    tmp = rand() % 100;
    key.ptr = &tmp;
    value.ptr = &tmp;
    switch (rand() % 5) {
    case 0: case 1:
      if (!ybc_item_set_item(task->cache, item, &key, &value)) {
        M_ERROR("error when storing item in the cache");
      }
      expect_value(item, &value);
      ybc_item_release(item);
      break;
    case 2:
      (void)ybc_item_remove(task->cache, &key);
      break;
    default:
      if (ybc_item_get(task->cache, item, &key)) {
        expect_value(item, &value);
        ybc_item_release(item);
      }
    }
  }
}

static void test_multithreaded_access(struct ybc *const cache,
    const size_t threads_count)
{
  m_open_anonymous(cache);

  struct p_thread threads[threads_count];
  struct thread_task task = {
      .cache = cache,
      .should_exit = 0,
  };

  for (size_t i = 0; i < threads_count; ++i) {
    p_thread_init_and_start(&threads[i], thread_func, &task);
  }

  p_sleep(300);
  task.should_exit = 1;

  for (size_t i = 0; i < threads_count; ++i) {
    p_thread_join_and_destroy(&threads[i]);
  }

  ybc_close(cache);
}

int main(void)
{
  char cache_buf[ybc_get_size()];
  struct ybc *const cache = (struct ybc *)cache_buf;

  test_anonymous_cache_create(cache);
  test_persistent_cache_create(cache);

  test_set_txn_ops(cache);
  test_item_ops(cache, 1000);
  test_expiration(cache);
  test_dogpile_effect_ops_async(cache);
  test_dogpile_effect_ops(cache);
  test_dogpile_effect_hashtable(cache);
  test_cluster_ops(5, 1000);
  test_simple_ops(cache);

  test_overlapped_acquirements(cache, 1000);
  test_interleaved_sets(cache);
  test_instant_clear(cache);
  test_persistent_survival(cache);
  test_broken_index_handling(cache);
  test_large_cache(cache);
  test_overwrite_protection(cache);
  test_out_of_memory(cache);
  test_data_compaction(cache);
  test_small_sync_interval(cache);

  test_disabled_hot_items_cache(cache);
  test_disabled_data_compaction(cache);
  test_disabled_syncing(cache);

  test_multithreaded_access(cache, 100);

  printf("All functional tests done\n");
  return 0;
}
