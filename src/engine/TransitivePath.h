// Copyright 2019, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: Florian Kramer (florian.kramer@neptun.uni-freiburg.de)

#pragma once

#include <memory>

#include "IdTable.h"
#include "Operation.h"
#include "QueryExecutionTree.h"

class TransitivePath : public Operation {
 public:
  TransitivePath(QueryExecutionContext* qec,
                 std::shared_ptr<QueryExecutionTree> child, bool leftIsVar,
                 bool rightIsVar, size_t leftSubCol, size_t rightSubCol,
                 size_t leftValue, size_t rightValue,
                 const std::string& leftColName,
                 const std::string& rightColName, size_t minDist,
                 size_t maxDist);

  /**
   * Returns a new TransitivePath operation that uses the fact that leftop
   * generates all possible values for the left side of the paths. If the
   * results of leftop is smaller than all possible values this will result in a
   * faster transitive path operation (as the transitive paths has to be
   * computed for fewer elements).
   */
  std::shared_ptr<TransitivePath> bindLeftSide(
      std::shared_ptr<QueryExecutionTree> leftop, size_t inputCol) const;

  /**
   * Returns a new TransitivePath operation that uses the fact that rightop
   * generates all possible values for the right side of the paths. If the
   * results of rightop is smaller than all possible values this will result in
   * a faster transitive path operation (as the transitive paths has to be
   * computed for fewer elements).
   */
  std::shared_ptr<TransitivePath> bindRightSide(
      std::shared_ptr<QueryExecutionTree> rightop, size_t inputCol) const;

  /**
   * Returns true if this tree was created using the bindLeftSide method.
   * Neither side of a tree may be bound twice
   */
  bool isBound() const;

  virtual std::string asString(size_t indent = 0) const override;

  virtual std::string getDescriptor() const override;

  virtual size_t getResultWidth() const override;

  virtual vector<size_t> resultSortedOn() const override;

  ad_utility::HashMap<std::string, size_t> getVariableColumns() const;

  virtual void setTextLimit(size_t limit) override;

  virtual bool knownEmptyResult() override;

  virtual float getMultiplicity(size_t col) override;

  virtual size_t getSizeEstimate() override;

  virtual size_t getCostEstimate() override;

  // The method is declared here to make it unit testable

  template <int SUB_WIDTH, bool leftIsVar, bool rightIsVar>
  static void computeTransitivePath(IdTable* res, const IdTable& sub,
                                    size_t leftSubCol, size_t rightSubCol,
                                    Id leftValue, Id rightValue, size_t minDist,
                                    size_t maxDist);

  template <int SUB_WIDTH>
  static void computeTransitivePath(IdTable* res, const IdTable& sub,
                                    bool leftIsVar, bool rightIsVar,
                                    size_t leftSubCol, size_t rightSubCol,
                                    Id leftValue, Id rightValue, size_t minDist,
                                    size_t maxDist);

  template <int SUB_WIDTH, int LEFT_WIDTH, int RES_WIDTH>
  static void computeTransitivePathLeftBound(
      IdTable* res, const IdTable& sub, const IdTable& left, size_t leftSideCol,
      bool rightIsVar, size_t leftSubCol, size_t rightSubCol, Id rightValue,
      size_t minDist, size_t maxDist, size_t resWidth);

  template <int SUB_WIDTH, int LEFT_WIDTH, int RES_WIDTH>
  static void computeTransitivePathRightBound(IdTable* res, const IdTable& sub,
                                              const IdTable& dynRight,
                                              size_t rightSideCol,
                                              bool leftIsVar, size_t leftSubCol,
                                              size_t rightSubCol, Id leftValue,
                                              size_t minDist, size_t maxDist,
                                              size_t resWidth);

 private:
  virtual void computeResult(ResultTable* result) override;

  // If this is not nullptr then the left side of all paths is within the result
  // of this tree.
  std::shared_ptr<QueryExecutionTree> _leftSideTree;
  size_t _leftSideCol;

  // If this is not nullptr then the right side of all paths is within the
  // result of this tree.
  std::shared_ptr<QueryExecutionTree> _rightSideTree;
  size_t _rightSideCol;

  size_t _resultWidth;
  ad_utility::HashMap<std::string, size_t> _variableColumns;

  std::shared_ptr<QueryExecutionTree> _subtree;
  bool _leftIsVar;
  bool _rightIsVar;
  size_t _leftSubCol;
  size_t _rightSubCol;
  size_t _leftValue;
  size_t _rightValue;
  std::string _leftColName;
  std::string _rightColName;
  size_t _minDist;
  size_t _maxDist;
};
