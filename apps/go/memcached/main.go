package main

import (
	"flag"
	"github.com/valyala/ybc/bindings/go/ybc"
	"github.com/valyala/ybc/libs/go/memcache"
	"log"
	"runtime"
	"time"
)

var (
	cacheFilePath     = flag.String("cacheFilePath", "", "Path to cache file. Leave empty for anonymous cache")
	cacheSize         = flag.Uint64("cacheSize", 100*1000*1000, "Cache size in bytes")
	deHashtableSize   = flag.Int("deHashtableSize", 1, "Dogpile effect hashtable size")
	goMaxProcs        = flag.Int("goMaxProcs", 4, "Maximum number of simultaneous go threads")
	hotDataSize       = flag.Uint64("hotDataSize", 0, "Hot data size in bytes. 0 disables hot data optimization")
	hotItemsCount     = flag.Uint64("hotItemsCount", 0, "The number of hot items. 0 disables hot items optimization")
	listenAddr        = flag.String("listenAddr", ":11211", "TCP address the daemon will listen to")
	maxItemsCount     = flag.Uint64("maxItemsCount", 1000*1000, "Maximum number of items to store in the cache")
	syncInterval      = flag.Int("syncInterval", 0, "Sync interval in milliseconds. 0 disables data syncing")
	osReadBufferSize  = flag.Int("osReadBufferSize", 4096, "Buffer size in bytes for incoming requests in OS")
	osWriteBufferSize = flag.Int("osWriteBufferSize", 4096, "Buffer size in bytes for outgoing responses in OS")
	readBufferSize    = flag.Int("readBufferSize", 4096, "Buffer size in bytes for incoming requests")
	writeBufferSize   = flag.Int("writeBufferSize", 4096, "Buffer size in bytes for outgoing responses")
)

func main() {
	flag.Parse()

	runtime.GOMAXPROCS(*goMaxProcs)

	hotItemsCount_ := ybc.SizeT(*hotItemsCount)
	if hotItemsCount_ == 0 {
		hotItemsCount_ = ybc.ConfigDisableHotItems
	}
	hotDataSize_ := ybc.SizeT(*hotDataSize)
	if hotDataSize_ == 0 {
		hotDataSize_ = ybc.ConfigDisableHotData
	}
	syncInterval_ := time.Duration(*syncInterval) * time.Millisecond
	if syncInterval_ <= 0 {
		syncInterval_ = ybc.ConfigDisableSync
	}
	config := &ybc.Config{
		MaxItemsCount:   ybc.SizeT(*maxItemsCount),
		DataFileSize:    ybc.SizeT(*cacheSize),
		HotItemsCount:   hotItemsCount_,
		HotDataSize:     hotDataSize_,
		DeHashtableSize: *deHashtableSize,
		SyncInterval:    syncInterval_,
	}

	if *cacheFilePath != "" {
		config.DataFile = *cacheFilePath + ".data"
		config.IndexFile = *cacheFilePath + ".index"
	}

	cache, err := config.OpenCache(true)
	if err != nil {
		log.Fatal("Cannot open cache: [%s]", err)
	}
	defer cache.Close()

	s := &memcache.Server{
		Cache:             cache,
		ListenAddr:        *listenAddr,
		ReadBufferSize:    *readBufferSize,
		WriteBufferSize:   *writeBufferSize,
		OSReadBufferSize:  *osReadBufferSize,
		OSWriteBufferSize: *osWriteBufferSize,
	}
	if err := s.Serve(); err != nil {
		log.Fatal("Cannot serve traffic: [%s]", err)
	}
}