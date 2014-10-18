#ifndef __BASE_DATAPATH__
#define __BASE_DATAPATH__

#include <boost/graph/graphviz.hpp>
#include <boost/config.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>
#include <boost/graph/topological_sort.hpp>
#include <boost/graph/iteration_macros.hpp>
#include <iostream>
#include <assert.h>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <map>
#include <list>
#include <set>
#include <stdint.h>

#include "DDDG.h"
#include "file_func.h"
#include "opcode_func.h"
#include "generic_func.h"
#include "MemoryInterface.h"
#include "Scratchpad.h"

#define CONTROL_EDGE 11
#define PIPE_EDGE 12

using namespace std;
typedef boost::property < boost::vertex_index_t, unsigned> VertexProperty;
typedef boost::property < boost::edge_name_t, uint8_t> EdgeProperty;
typedef boost::adjacency_list < boost::listS, boost::vecS, boost::bidirectionalS, VertexProperty, EdgeProperty> Graph;
typedef boost::graph_traits<Graph>::vertex_descriptor Vertex;
typedef boost::graph_traits<Graph>::edge_descriptor Edge;
typedef boost::graph_traits<Graph>::vertex_iterator vertex_iter;
typedef boost::graph_traits<Graph>::edge_iterator edge_iter;
typedef boost::graph_traits<Graph>::in_edge_iterator in_edge_iter;
typedef boost::graph_traits<Graph>::out_edge_iterator out_edge_iter;
typedef boost::property_map<Graph, boost::edge_name_t>::type EdgeNameMap;
typedef boost::property_map<Graph, boost::vertex_index_t>::type VertexNameMap;
// Used heavily in reporting cycle-level statistics.
typedef std::unordered_map< std::string, std::vector<int> > activity_map;

class Scratchpad;

struct partitionEntry
{
  std::string type;
  unsigned array_size; //num of elements
  unsigned part_factor;
};
struct regEntry
{
  int size;
  int reads;
  int writes;
};
struct callDep
{
  std::string caller;
  std::string callee;
  int callInstID;
};
struct newEdge
{
  unsigned from;
  unsigned to;
  int parid;
};
struct RQEntry
{
  unsigned node_id;
  mutable float latency_so_far;
  mutable bool valid;
};

struct RQEntryComp
{
  bool operator() (const RQEntry& left, const RQEntry &right) const
  { return left.node_id < right.node_id; }
};

class BaseDatapath
{
 public:
  BaseDatapath(std::string bench, string trace_file, string config_file, float cycle_t);
  virtual ~BaseDatapath();
  void addDddgEdge(unsigned int from, unsigned int to, uint8_t parid);
  void insertMicroop(int node_microop) { microop.push_back(node_microop);}
  void setGlobalGraph();
  void setGraphForStepping();
  int clearGraph();
  void parse_config(std::string bench, std::string config_file);

  //Accessing stats
  int getNumOfNodes() {return boost::num_vertices(graph_);}
  int getNumOfEdges() {return boost::num_edges(graph_);}
  int getMicroop(unsigned int node_id) {return microop.at(node_id);}
  int getNumOfConnectedNodes(unsigned int node_id) {return boost::degree(nameToVertex[node_id], graph_);}
  int getUnrolledLoopBoundary(unsigned int region_id) {return loopBound.at(region_id);}
  bool doesEdgeExist(unsigned int from, unsigned int to) {return edge(nameToVertex[from], nameToVertex[to], graph_).second;}
  std::string getBenchName() {return benchName;}
  std::string getBaseAddressLabel(unsigned int node_id) {return baseAddress[node_id].first;}
  int shortestDistanceBetweenNodes(unsigned int from, unsigned int to);

  // Graph optimizations.
  void removeInductionDependence();
  void removePhiNodes();
  void memoryAmbiguation();
  void removeAddressCalculation();
  void removeBranchEdges();
  void nodeStrengthReduction();
  void loopFlatten();
  void loopUnrolling();
  void loopPipelining();
  void storeBuffer();
  void removeSharedLoads();
  void removeRepeatedStores();
  void treeHeightReduction();

 protected:

  void clearGlobalGraph();
  void findMinRankNodes(
      unsigned &node1, unsigned &node2, std::map<unsigned, unsigned> &rank_map);
  void cleanLeafNodes();
  bool doesEdgeExistVertex(Vertex from, Vertex to) {return edge(from, to, graph_).second;}

  // Configuration parsing and handling.
  bool readPipeliningConfig();
  bool readUnrollingConfig(std::unordered_map<int, int > &unrolling_config);
  bool readFlattenConfig(std::unordered_set<int> &flatten_config);
  bool readPartitionConfig(std::unordered_map<std::string,
         partitionEntry> & partition_config);
  bool readCompletePartitionConfig(std::unordered_map<std::string, unsigned> &config);

  // State initialization.
  void initMethodID(std::vector<int> &methodid);
  void initDynamicMethodID(std::vector<std::string> &methodid);
  void initPrevBasicBlock(std::vector<std::string> &prevBasicBlock);
  void initInstID(std::vector<std::string> &instid);
  void initAddressAndSize(
      std::unordered_map<unsigned, pair<long long int, unsigned> > &address);
  void initAddress(std::unordered_map<unsigned, long long int> &address);
  void initLineNum(std::vector<int> &lineNum);
  void initGetElementPtr(
      std::unordered_map<unsigned, pair<std::string, long long int> > &get_element_ptr);

  void updateGraphWithIsolatedEdges(std::vector<Edge> &to_remove_edges);
  void updateGraphWithNewEdges(std::vector<newEdge> &to_add_edges);
  void updateGraphWithIsolatedNodes(std::unordered_set<unsigned> &to_remove_nodes);
  void updateChildren(unsigned node_id);
  void updateRegStats();
  
  int fireMemNodes();
  int fireNonMemNodes();
  void copyToExecutingQueue();
  void initExecutingQueue();
  void addMemReadyNode( unsigned node_id, float latency_so_far);
  void addNonMemReadyNode( unsigned node_id, float latency_so_far);

  // Stats output.
  void writeFinalLevel();
  void writeGlobalIsolated();
  void writePerCycleActivity(MemoryInterface* memory);
  void writeBaseAddress();
  void writeMicroop(std::vector<int> &microop);

  virtual bool step();
  virtual void dumpStats();
  virtual void stepExecutingQueue() = 0;
  virtual void globalOptimizationPass() = 0;

  //global/whole datapath variables
  std::vector<int> newLevel;
  std::vector<regEntry> regStats;
  std::vector<int> microop;
  std::unordered_map<unsigned, pair<std::string, long long int> > baseAddress;

  unsigned numTotalNodes;
  unsigned numTotalEdges;

  char* benchName;

  int cycle;
  float cycleTime;

  Graph graph_;
  std::unordered_map<unsigned, Vertex> nameToVertex;
  VertexNameMap vertexToName;
  EdgeNameMap edgeToParid;

  std::vector<int> numParents;
  std::vector<float> latestParents;
  std::vector<bool> finalIsolated;
  std::vector<int> edgeLatency;
  std::vector<int> loopBound;

  std::unordered_set<std::string> dynamicMemoryOps;
  std::unordered_set<std::string> functionNames;

  //stateful states
  unsigned totalConnectedNodes;
  unsigned executedNodes;

  std::vector<unsigned> executingQueue;
  std::vector<unsigned> readyToExecuteQueue;
};


#endif