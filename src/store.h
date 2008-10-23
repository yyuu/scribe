//  Copyright (c) 2007-2008 Facebook
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//
// See accompanying file LICENSE or visit the Scribe site at:
// http://developers.facebook.com/scribe/ 
//
// @author Bobby Johnson
// @author James Wang
// @author Jason Sobel
// @author Alex Moskalyuk
// @author Avinash Lakshman
// @author Anthony Giardullo

#ifndef SCRIBE_STORE_H
#define SCRIBE_STORE_H

#include "common.h" // includes std libs, thrift, and stl typedefs
#include "conf.h"
#include "file.h"
#include "conn_pool.h"

/* defines used by the store class */
enum roll_period_t {
  ROLL_NEVER,
  ROLL_HOURLY,
  ROLL_DAILY
};


/*
 * Abstract class to define the interface for a store
 * and implement some basic functionality.
 */
class Store {
 public:
  // Creates an object of the appropriate subclass.
  static boost::shared_ptr<Store> 
    createStore(const std::string& type, const std::string& category, 
                bool readable = false, bool multi_category = false);

  Store(const std::string& category, const std::string &type, 
        bool multi_category = false);
  virtual ~Store();

  virtual boost::shared_ptr<Store> copy(const std::string &category) = 0;
  virtual bool open() = 0;
  virtual bool isOpen() = 0;
  virtual void configure(pStoreConf configuration) = 0;
  virtual void close() = 0;

  // Attempts to store messages and returns true if successful.
  // On failure, returns false and messages contains any un-processed messages
  virtual bool handleMessages(boost::shared_ptr<logentry_vector_t> messages) = 0;
  virtual void periodicCheck() {}
  virtual void flush() = 0;

  virtual std::string getStatus();

  // following methods must be overidden to make a store readable
  virtual bool readOldest(/*out*/ boost::shared_ptr<logentry_vector_t> messages,
                          struct tm* now);
  virtual void deleteOldest(struct tm* now);
  virtual bool replaceOldest(boost::shared_ptr<logentry_vector_t> messages,
                             struct tm* now);
  virtual bool empty(struct tm* now);

  // don't need to override
  virtual const std::string& getType();

 protected:
  virtual void setStatus(const std::string& new_status);
  std::string status;
  std::string categoryHandled;
  bool multiCategory;             // Whether multiple categories are handled
  std::string storeType;

  // Don't ever take this lock for multiple stores at the same time
  pthread_mutex_t statusMutex;

 private:
  // disallow copy, assignment, and empty construction
  Store(Store& rhs);
  Store& operator=(Store& rhs);
};

/*
 * Abstract class that serves as a base for file-based stores.
 * This class has logic for naming files and deciding when to rotate.
 */
class FileStoreBase : public Store {
 public:
  FileStoreBase(const std::string& category, const std::string &type, 
                bool multi_category);
  ~FileStoreBase();

  virtual void copyCommon(const FileStoreBase *base);
  bool open();
  void configure(pStoreConf configuration);
  void periodicCheck();

 protected:
  // We need to pass arguments to open when called internally.
  // The external open function just calls this with default args.
  virtual bool openInternal(bool incrementFilename, struct tm* current_time) = 0;
  virtual void rotateFile(struct tm *timeinfo);

  // appends information about the current file to a log file in the same directory
  virtual void printStats();

  // Returns the number of bytes to pad to align to the specified block size
  unsigned long bytesToPad(unsigned long next_message_length, 
                           unsigned long current_file_size, 
                           unsigned long chunk_size);

  // A full filename includes an absolute path and a sequence number suffix.
  std::string makeBaseFilename(struct tm* creation_time = NULL);
  std::string makeFullFilename(int suffix, struct tm* creation_time = NULL);
  std::string makeBaseSymlink();
  std::string makeFullSymlink();
  int findOldestFile(const std::string& base_filename);
  int findNewestFile(const std::string& base_filename);
  int getFileSuffix(const std::string& filename, const std::string& base_filename);

  // Configuration
  std::string filePath;
  std::string baseFileName;
  unsigned long maxSize;
  roll_period_t rollPeriod;
  unsigned long rollHour;
  unsigned long rollMinute;
  std::string fsType;
  unsigned long chunkSize;
  bool writeMeta;
  bool writeCategory;
  bool createSymlink;

  // State
  unsigned long currentSize;
  int lastRollTime; // either hour or day, depending on rollPeriod
  std::string currentFilename; // this isn't used to choose the next file name, we just need it for reporting
  unsigned long eventsWritten; // This is how many events this process has
                               // written to the currently open file. It is NOT
                               // necessarily the number of lines in the file

 private:
  // disallow copy, assignment, and empty construction
  FileStoreBase(FileStoreBase& rhs);
  FileStoreBase& operator=(FileStoreBase& rhs);
};

/* 
 * This file-based store uses an instance of a FileInterface class that
 * handles the details of interfacing with the filesystem. (see file.h)
 */
class FileStore : public FileStoreBase {

 public:
  FileStore(const std::string& category, bool multi_category, 
            bool is_buffer_file = false);
  ~FileStore();

  boost::shared_ptr<Store> copy(const std::string &category);
  bool handleMessages(boost::shared_ptr<logentry_vector_t> messages);
  bool isOpen();
  void configure(pStoreConf configuration);
  void close();
  void flush();

  // Each read does its own open and close and gets the whole file.
  // This is separate from the write file, and not really a consistent interface.
  bool readOldest(/*out*/ boost::shared_ptr<logentry_vector_t> messages,
                  struct tm* now);
  virtual bool replaceOldest(boost::shared_ptr<logentry_vector_t> messages,
                             struct tm* now);
  void deleteOldest(struct tm* now);
  bool empty(struct tm* now);

 protected:
  // Implement FileStoreBase virtual function
  bool openInternal(bool incrementFilename, struct tm* current_time);
  bool writeMessages(boost::shared_ptr<logentry_vector_t> messages,
                     boost::shared_ptr<FileInterface> write_file);

  bool isBufferFile;
  bool addNewlines;

  // State
  boost::shared_ptr<FileInterface> writeFile;

 private:
  // disallow copy, assignment, and empty construction
  FileStore(FileStore& rhs);
  FileStore& operator=(FileStore& rhs);
};

/*
 * This file-based store relies on thrift's TFileTransport to do the writing
 */
class ThriftFileStore : public FileStoreBase {
 public:
  ThriftFileStore(const std::string& category, bool multi_category);
  ~ThriftFileStore();

  boost::shared_ptr<Store> copy(const std::string &category);
  bool handleMessages(boost::shared_ptr<logentry_vector_t> messages);
  bool open();
  bool isOpen();
  void configure(pStoreConf configuration);
  void close();
  void flush();

 protected:
  // Implement FileStoreBase virtual function
  bool openInternal(bool incrementFilename, struct tm* current_time);

  boost::shared_ptr<facebook::thrift::transport::TFileTransport> thriftFileTransport;

  unsigned long flushFrequencyMs;
  unsigned long msgBufferSize;

 private:
  // disallow copy, assignment, and empty construction
  ThriftFileStore(ThriftFileStore& rhs);
  ThriftFileStore& operator=(ThriftFileStore& rhs);
};

/* 
 * This store aggregates messages and sends them to another store
 * in larger groups. If it is unable to do this it saves them to
 * a secondary store, then reads them and sends them to the
 * primary store when it's back online.
 *
 * This actually involves two buffers. Messages are always buffered
 * briefly in memory, then they're buffered to a secondary store if
 * the primary store is down.
 */
class BufferStore : public Store {
 
 public:
  BufferStore(const std::string& category, bool multi_category);
  ~BufferStore();

  boost::shared_ptr<Store> copy(const std::string &category);
  bool handleMessages(boost::shared_ptr<logentry_vector_t> messages);
  bool open();
  bool isOpen();
  void configure(pStoreConf configuration);
  void close();
  void flush();
  void periodicCheck();

  std::string getStatus();

 protected:
  // Store we're trying to get the messages to
  boost::shared_ptr<Store> primaryStore;

  // Store to use as a buffer if the primary is unavailable.
  // The store must be of a type that supports reading.
  boost::shared_ptr<Store> secondaryStore;

  // buffer state machine
  enum buffer_state_t {
    STREAMING,       // connected to primary and sending directly
    DISCONNECTED,    // disconnected and writing to secondary
    SENDING_BUFFER,  // connected to primary and sending data from secondary
  };

  void changeState(buffer_state_t new_state); // handles state pre and post conditions
  const char* stateAsString(buffer_state_t state);

  time_t getNewRetryInterval(); // generates a random interval based on config

  // configuration
  unsigned long maxQueueLength;   // in number of messages
  unsigned long bufferSendRate;   // number of buffer files sent each periodicCheck
  time_t avgRetryInterval;        // in seconds, for retrying primary store open
  time_t retryIntervalRange;      // in seconds

  // state
  buffer_state_t state;
  time_t lastWriteTime;
  time_t lastOpenAttempt;
  time_t retryInterval;

 private:
  // disallow copy, assignment, and empty construction
  BufferStore();
  BufferStore(BufferStore& rhs);
  BufferStore& operator=(BufferStore& rhs);
};

/*
 * This store sends messages to another scribe server.
 * This class is really just an adapter to the global 
 * connection pool g_connPool.
 */
class NetworkStore : public Store {
 
 public:
  NetworkStore(const std::string& category, bool multi_category);
  ~NetworkStore();

  boost::shared_ptr<Store> copy(const std::string &category);
  bool handleMessages(boost::shared_ptr<logentry_vector_t> messages);
  bool open();
  bool isOpen();
  void configure(pStoreConf configuration);
  void close();
  void flush();

 protected:
  static const long int DEFAULT_SOCKET_TIMEOUT_MS = 5000; // 5 sec timeout

  // configuration
  bool useConnPool;
  bool smcBased;
  long int timeout;
  std::string remoteHost;
  unsigned long remotePort; // long because it works with config code
  std::string smcService;

  // state
  bool opened;
  boost::shared_ptr<scribeConn> unpooledConn; // null if useConnPool

 private:
  // disallow copy, assignment, and empty construction
  NetworkStore();
  NetworkStore(NetworkStore& rhs);
  NetworkStore& operator=(NetworkStore& rhs);
};

/*
 * This store separates messages into many groups based on a 
 * hash function, and sends each group to a different store.
 */
class BucketStore : public Store {
 
 public:
  BucketStore(const std::string& category, bool multi_category);
  ~BucketStore();

  boost::shared_ptr<Store> copy(const std::string &category);
  bool handleMessages(boost::shared_ptr<logentry_vector_t> messages);
  bool open();
  bool isOpen();
  void configure(pStoreConf configuration);
  void close();
  void flush();
  void periodicCheck();

  std::string getStatus();

 protected:
  enum bucketizer_type {
    context_log,
    key_hash,
    key_modulo
  };

  bucketizer_type bucketType;
  char delimiter;
  bool removeKey;
  bool opened;
  unsigned long numBuckets;
  std::vector<boost::shared_ptr<Store> > buckets;

  unsigned long bucketize(const std::string& message);
  std::string getMessageWithoutKey(const std::string& message);

 private:
  // disallow copy, assignment, and emtpy construction
  BucketStore();
  BucketStore(BucketStore& rhs);
  BucketStore& operator=(BucketStore& rhs);
};

/*
 * This store intentionally left blank.
 */
class NullStore : public Store {

 public:
  NullStore(const std::string& category, bool multi_category);
  virtual ~NullStore();

  boost::shared_ptr<Store> copy(const std::string &category);
  bool open();
  bool isOpen();
  void configure(pStoreConf configuration);
  void close();

  bool handleMessages(boost::shared_ptr<logentry_vector_t> messages);
  void flush();

  // null stores are readable, but you never get anything
  virtual bool readOldest(/*out*/ boost::shared_ptr<logentry_vector_t> messages,                          struct tm* now);
  virtual bool replaceOldest(boost::shared_ptr<logentry_vector_t> messages,
                             struct tm* now);
  virtual void deleteOldest(struct tm* now);
  virtual bool empty(struct tm* now);


 private:
  // disallow empty constructor, copy and assignment
  NullStore();
  NullStore(Store& rhs);
  NullStore& operator=(Store& rhs);
};

/*
 * This store relays messages to n other stores
 * @author Joel Seligstein
 */
class MultiStore : public Store {
 public:
  MultiStore(const std::string& category, bool multi_category);
  ~MultiStore();

  boost::shared_ptr<Store> copy(const std::string &category);
  bool open();
  bool isOpen();
  void configure(pStoreConf configuration);
  void close();

  bool handleMessages(boost::shared_ptr<logentry_vector_t> messages);
  void periodicCheck();
  void flush();

  // read won't make sense since we don't know which store to read from
  bool readOldest(/*out*/ boost::shared_ptr<logentry_vector_t> messages,
                  struct tm* now) { return false; }
  void deleteOldest(struct tm* now) {}
  bool empty(struct tm* now) { return true; }

 protected:
  std::vector<boost::shared_ptr<Store> > stores;
  enum report_success_value {
    SUCCESS_ANY = 1,
    SUCCESS_ALL
  };
  report_success_value report_success;

 private:
  // disallow copy, assignment, and empty construction
  MultiStore();
  MultiStore(Store& rhs);
  MultiStore& operator=(Store& rhs);
};


/*
 * This store will contain a separate store for every distinct
 * category it encounters.
 *
 */
class CategoryStore : public Store {
 public:
  CategoryStore(const std::string& category, bool multi_category);
  CategoryStore(const std::string& category, const std::string& name,
                bool multiCategory);
  ~CategoryStore();

  boost::shared_ptr<Store> copy(const std::string &category);
  bool open();
  bool isOpen();
  void configure(pStoreConf configuration);
  void close();

  bool handleMessages(boost::shared_ptr<logentry_vector_t> messages);
  void periodicCheck();
  void flush();

 protected:
  void configureCommon(pStoreConf configuration, const std::string type);
  boost::shared_ptr<Store> modelStore;
  std::map<std::string, boost::shared_ptr<Store> > stores;

 private:
  CategoryStore();
  CategoryStore(Store& rhs);
  CategoryStore& operator=(Store& rhs);
};

/*
 * MultiFileStore is similar to FileStore except that it uses a separate file
 * for every category.  This is useful only if this store can handle mutliple
 * categories.
 */
class MultiFileStore : public CategoryStore {
 public:
  MultiFileStore(const std::string& category, bool multi_category);
  ~MultiFileStore();
  void configure(pStoreConf configuration);

 private:
  MultiFileStore();
  MultiFileStore(Store& rhs);
  MultiFileStore& operator=(Store& rhs);
};

/*
 * ThriftMultiFileStore is similar to ThriftFileStore except that it uses a
 * separate thrift file for every category.  This is useful only if this store
 * can handle mutliple categories.
 */
class ThriftMultiFileStore : public CategoryStore {
 public:
  ThriftMultiFileStore(const std::string& category, bool multi_category);
  ~ThriftMultiFileStore();
  void configure(pStoreConf configuration);


 private:
  ThriftMultiFileStore();
  ThriftMultiFileStore(Store& rhs);
  ThriftMultiFileStore& operator=(Store& rhs);
};
#endif // SCRIBE_STORE_H