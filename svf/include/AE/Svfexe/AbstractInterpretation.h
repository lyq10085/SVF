//===- AbstractInterpretation.h -- Abstract Execution----------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013->  <Yulei Sui>
//

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//


//
// Created by Jiawei Wang on 2024/1/10.
// The implementation is based on
// Xiao Cheng, Jiawei Wang and Yulei Sui. Precise Sparse Abstract Execution via Cross-Domain Interaction.
// 46th International Conference on Software Engineering. (ICSE24)
//
#pragma once
#include "AE/Core/AbstractState.h"
#include "AE/Core/ICFGWTO.h"
#include "AE/Svfexe/AEDetector.h"
#include "AE/Svfexe/AbsExtAPI.h"
#include "Util/SVFBugReport.h"
#include "WPA/Andersen.h"

namespace SVF
{
class AbstractInterpretation;
class AbsExtAPI;
class AEStat;
class AEAPI;

template<typename T> class FILOWorkList;

/// AEStat: Statistic for AE
class AEStat : public SVFStat
{
public:
    void countStateSize();
    AEStat(AbstractInterpretation* ae) : _ae(ae)
    {
        startTime = getClk(true);
    }
    ~AEStat()
    {
    }
    inline std::string getMemUsage()
    {
        u32_t vmrss, vmsize;
        return SVFUtil::getMemoryUsageKB(&vmrss, &vmsize) ? std::to_string(vmsize) + "KB" : "cannot read memory usage";
    }

    void finializeStat();
    void performStat() override;

public:
    AbstractInterpretation* _ae;
    s32_t count{0};
    std::string memory_usage;
    std::string memUsage;


    u32_t& getFunctionTrace()
    {
        if (generalNumMap.count("Function_Trace") == 0)
        {
            generalNumMap["Function_Trace"] = 0;
        }
        return generalNumMap["Function_Trace"];
    }
    u32_t& getBlockTrace()
    {
        if (generalNumMap.count("Block_Trace") == 0)
        {
            generalNumMap["Block_Trace"] = 0;
        }
        return generalNumMap["Block_Trace"];
    }
    u32_t& getICFGNodeTrace()
    {
        if (generalNumMap.count("ICFG_Node_Trace") == 0)
        {
            generalNumMap["ICFG_Node_Trace"] = 0;
        }
        return generalNumMap["ICFG_Node_Trace"];
    }
};

/// AbstractInterpretation is same as Abstract Execution
class AbstractInterpretation
{
    friend class AEStat;
    friend class AEAPI;
    friend class BufOverflowDetector;

public:
    typedef SCCDetection<PTACallGraph*> CallGraphSCC;
    /// Constructor
    AbstractInterpretation();

    virtual void runOnModule(ICFG* icfg);

    /// Destructor
    virtual ~AbstractInterpretation();

    /// Program entry
    void analyse();

    static AbstractInterpretation& getAEInstance()
    {
        static AbstractInterpretation instance;
        return instance;
    }

    void addDetector(std::unique_ptr<AEDetector> detector)
    {
        detectors.push_back(std::move(detector));
    }

    Set<const CallICFGNode*> checkpoints; // for CI check



    void printAllAbsStates() const {

        for(const auto& [node, state] : abstractTrace) {
            std::cout << "--------------------------------" << std::endl;
            std::cout << "node: " << node->toString() << std::endl;
            state.printAbstractState();
        }
    }


    bool checkLineNumber(const std::string& str, int i) const {
    // 查找 "ln": 的位置
        size_t ln_pos = str.find("\"ln\":");
        if (ln_pos == std::string::npos) return false;
        
        // 找到数字开始的位置（跳过空格）
        ln_pos += 5;  // "ln": 的长度
        while (ln_pos < str.length() && str[ln_pos] == ' ') ln_pos++;
        
        // 找到数字结束的位置（逗号或其他非数字字符）
        size_t end_pos = ln_pos;
        while (end_pos < str.length() && isdigit(str[end_pos])) end_pos++;
        
        // 提取数字并比较
        std::string num_str = str.substr(ln_pos, end_pos - ln_pos);
        int ln = std::stoi(num_str);
        
        return ln == i;
    }
    
    bool checkFileName(const std::string& str, const std::string& filename) const {
        // 查找 "fl": 的位置
        size_t fl_pos = str.find("\"fl\":");
        if (fl_pos == std::string::npos) return false;
        
        // 找到文件名开始的位置（跳过空格和引号）
        fl_pos += 5;  // "fl": 的长度
        while (fl_pos < str.length() && (str[fl_pos] == ' ' || str[fl_pos] == '\"')) fl_pos++;
        
        // 找到文件名结束的位置（引号）
        size_t end_pos = fl_pos;
        while (end_pos < str.length() && str[end_pos] != '\"') end_pos++;
        
        // 提取文件名并比较
        std::string file_str = str.substr(fl_pos, end_pos - fl_pos);
        
        return file_str == filename;
    }

    static Map<std::string, std::string>& getVarValue() {
        static Map<std::string, std::string> varValues;
        return varValues;
    }


    static Set<std::string>& getVarNames()  {
        static Set<std::string> varnames;
        return varnames;
    }

    static void addVarName(const std::string& name) {
        getVarNames().insert(name);
    }

    void printOneLineAbsState(const std::string& filename, int line, Set<std::string>& varNames) const {
        for (const auto& pair : abstractTrace) {
            const ICFGNode* n = pair.first;
            // Skip nodes that are not call sites
            if (!SVFUtil::isa<CallICFGNode>(n)) {
                continue;
            }
            const CallICFGNode* callNode = SVFUtil::dyn_cast<CallICFGNode>(n);
            if (!callNode) {
                continue;
            }
            if (checkFileName(callNode->getSourceLoc(), filename) && checkLineNumber(callNode->getSourceLoc(), line)) {  // targetLine是你想查找的行号
                std::cout << "--------------------------------" << std::endl;
                std::cout << "callsite node: " << n->toString() << std::endl;
                for (const auto& varName : varNames) {
                    std::cout << "varName: " << varName << std::endl;
                    addVarName(varName);
                }
                // 开始打印参数值
                const AbstractState& as = pair.second;
                as.printAbstractState();
                for (const auto& [varName, varValue] : getVarValue()) {
                    std::cout << "varName: " << varName << ", varValue: " << varValue << std::endl;
                }
            }
        }
    }




    // void printAbsValue(u32_t varId) const {
    //     for(const auto& [node, state] : abstractTrace) {
    //         if(state.hasVar(varId)) {
    //             std::cout << "Node: " << node->toString() << std::endl;
    //             std::cout << "Var " << varId << ": " << state[varId] << std::endl;
    //         }
    //     }
    // }


private:
    /// Global ICFGNode is handled at the entry of the program,
    virtual void handleGlobalNode();

    /// Mark recursive functions in the call graph
    void initWTO();

    /**
     * Check if execution state exist by merging states of predecessor nodes
     *
     * @param icfgNode The icfg node to analyse
     * @return if this node has preceding execution state
     */
    bool mergeStatesFromPredecessors(const ICFGNode * icfgNode);

    /**
     * Check if execution state exist at the branch edge
     *
     * @param intraEdge the edge from CmpStmt to the next node
     * @return if this edge is feasible
     */
    bool isBranchFeasible(const IntraCFGEdge* intraEdge, AbstractState& as);

    /**
     * handle instructions in ICFGSingletonWTO
     *
     * @param block basic block that has one instruction or a series of instructions
     */
    virtual void handleSingletonWTO(const ICFGSingletonWTO *icfgSingletonWto);

    /**
     * handle call node in ICFGNode
     *
     * @param node ICFGNode which has a single CallICFGNode
     */
    virtual void handleCallSite(const ICFGNode* node);

    /**
     * handle wto cycle (loop)
     *
     * @param cycle WTOCycle which has weak topo order of basic blocks and nested cycles
     */
    virtual void handleCycleWTO(const ICFGCycleWTO* cycle);

    void handleWTOComponents(const std::list<const ICFGWTOComp*>& wtoComps);

    void handleWTOComponent(const ICFGWTOComp* wtoComp);


    /**
     * handle SVF Statement like CmpStmt, CallStmt, GepStmt, LoadStmt, StoreStmt, etc.
     *
     * @param stmt SVFStatement which is a value flow of instruction
     */
    virtual void handleSVFStatement(const SVFStmt* stmt);

    /**
     * Check if this callnode is recursive call and skip it.
     *
     * @param callnode CallICFGNode which calls a recursive function
     */
    virtual void SkipRecursiveCall(const CallICFGNode* callnode);


    /**
    * Check if this cmpStmt and succ are satisfiable to the execution state.
    *
    * @param cmpStmt CmpStmt is a conditional branch statement
    * @param succ the value of cmpStmt (True or False)
    * @return if this ICFGNode has preceding execution state
    */
    bool isCmpBranchFeasible(const CmpStmt* cmpStmt, s64_t succ,
                             AbstractState& as);

    /**
    * Check if this SwitchInst and succ are satisfiable to the execution state.
    *
    * @param var var in switch inst
    * @param succ the case value of switch inst
    * @return if this ICFGNode has preceding execution state
    */
    bool isSwitchBranchFeasible(const SVFVar* var, s64_t succ,
                                AbstractState& as);


    void collectCheckPoint();
    void checkPointAllSet();

    void updateStateOnAddr(const AddrStmt *addr);

    void updateStateOnBinary(const BinaryOPStmt *binary);

    void updateStateOnCmp(const CmpStmt *cmp);

    void updateStateOnLoad(const LoadStmt *load);

    void updateStateOnStore(const StoreStmt *store);

    void updateStateOnCopy(const CopyStmt *copy);

    void updateStateOnCall(const CallPE *callPE);

    void updateStateOnRet(const RetPE *retPE);

    void updateStateOnGep(const GepStmt *gep);

    void updateStateOnSelect(const SelectStmt *select);

    void updateStateOnPhi(const PhiStmt *phi);


    /// protected data members, also used in subclasses
    SVFIR* svfir;
    /// Execution State, used to store the Interval Value of every SVF variable
    AEAPI* api{nullptr};

    ICFG* icfg;
    AEStat* stat;

    std::vector<const CallICFGNode*> callSiteStack;
    Map<const CallGraphNode*, ICFGWTO*> funcToWTO;
    Set<const CallGraphNode*> recursiveFuns;


    AbstractState& getAbsStateFromTrace(const ICFGNode* node)
    {
        const ICFGNode* repNode = icfg->getRepNode(node);
        if (abstractTrace.count(repNode) == 0)
        {
            assert(0 && "No preAbsTrace for this node");
        }
        else
        {
            return abstractTrace[repNode];
        }
    }

    bool hasAbsStateFromTrace(const ICFGNode* node)
    {
        const ICFGNode* repNode = icfg->getRepNode(node);
        return abstractTrace.count(repNode) != 0;
    }

    AbsExtAPI* getUtils()
    {
        return utils;
    }

    // helper functions in handleCallSite
    virtual bool isExtCall(const CallICFGNode* callNode);
    virtual void extCallPass(const CallICFGNode* callNode);
    virtual bool isRecursiveCall(const CallICFGNode* callNode);
    virtual void recursiveCallPass(const CallICFGNode* callNode);
    virtual bool isDirectCall(const CallICFGNode* callNode);
    virtual void directCallFunPass(const CallICFGNode* callNode);
    virtual bool isIndirectCall(const CallICFGNode* callNode);
    virtual void indirectCallFunPass(const CallICFGNode* callNode);

    // there data should be shared with subclasses
    Map<std::string, std::function<void(const CallICFGNode*)>> func_map;

    Map<const ICFGNode*, AbstractState> abstractTrace; // abstract states immediately after nodes
    std::string moduleName;

    std::vector<std::unique_ptr<AEDetector>> detectors;
    AbsExtAPI* utils;

    // according to varieties of cmp insts,
    // maybe var X var, var X const, const X var, const X const
    // we accept 'var X const' 'var X var' 'const X const'
    // if 'const X var', we need to reverse op0 op1 and its predicate 'var X' const'
    // X' is reverse predicate of X
    // == -> !=, != -> ==, > -> <=, >= -> <, < -> >=, <= -> >

    Map<s32_t, s32_t> _reverse_predicate =
    {
        {CmpStmt::Predicate::FCMP_OEQ, CmpStmt::Predicate::FCMP_ONE},  // == -> !=
        {CmpStmt::Predicate::FCMP_UEQ, CmpStmt::Predicate::FCMP_UNE},  // == -> !=
        {CmpStmt::Predicate::FCMP_OGT, CmpStmt::Predicate::FCMP_OLE},  // > -> <=
        {CmpStmt::Predicate::FCMP_OGE, CmpStmt::Predicate::FCMP_OLT},  // >= -> <
        {CmpStmt::Predicate::FCMP_OLT, CmpStmt::Predicate::FCMP_OGE},  // < -> >=
        {CmpStmt::Predicate::FCMP_OLE, CmpStmt::Predicate::FCMP_OGT},  // <= -> >
        {CmpStmt::Predicate::FCMP_ONE, CmpStmt::Predicate::FCMP_OEQ},  // != -> ==
        {CmpStmt::Predicate::FCMP_UNE, CmpStmt::Predicate::FCMP_UEQ},  // != -> ==
        {CmpStmt::Predicate::ICMP_EQ, CmpStmt::Predicate::ICMP_NE},  // == -> !=
        {CmpStmt::Predicate::ICMP_NE, CmpStmt::Predicate::ICMP_EQ},  // != -> ==
        {CmpStmt::Predicate::ICMP_UGT, CmpStmt::Predicate::ICMP_ULE},  // > -> <=
        {CmpStmt::Predicate::ICMP_ULT, CmpStmt::Predicate::ICMP_UGE},  // < -> >=
        {CmpStmt::Predicate::ICMP_UGE, CmpStmt::Predicate::ICMP_ULT},  // >= -> <
        {CmpStmt::Predicate::ICMP_SGT, CmpStmt::Predicate::ICMP_SLE},  // > -> <=
        {CmpStmt::Predicate::ICMP_SLT, CmpStmt::Predicate::ICMP_SGE},  // < -> >=
        {CmpStmt::Predicate::ICMP_SGE, CmpStmt::Predicate::ICMP_SLT},  // >= -> <
    };


    Map<s32_t, s32_t> _switch_lhsrhs_predicate =
    {
        {CmpStmt::Predicate::FCMP_OEQ, CmpStmt::Predicate::FCMP_OEQ},  // == -> ==
        {CmpStmt::Predicate::FCMP_UEQ, CmpStmt::Predicate::FCMP_UEQ},  // == -> ==
        {CmpStmt::Predicate::FCMP_OGT, CmpStmt::Predicate::FCMP_OLT},  // > -> <
        {CmpStmt::Predicate::FCMP_OGE, CmpStmt::Predicate::FCMP_OLE},  // >= -> <=
        {CmpStmt::Predicate::FCMP_OLT, CmpStmt::Predicate::FCMP_OGT},  // < -> >
        {CmpStmt::Predicate::FCMP_OLE, CmpStmt::Predicate::FCMP_OGE},  // <= -> >=
        {CmpStmt::Predicate::FCMP_ONE, CmpStmt::Predicate::FCMP_ONE},  // != -> !=
        {CmpStmt::Predicate::FCMP_UNE, CmpStmt::Predicate::FCMP_UNE},  // != -> !=
        {CmpStmt::Predicate::ICMP_EQ, CmpStmt::Predicate::ICMP_EQ},  // == -> ==
        {CmpStmt::Predicate::ICMP_NE, CmpStmt::Predicate::ICMP_NE},  // != -> !=
        {CmpStmt::Predicate::ICMP_UGT, CmpStmt::Predicate::ICMP_ULT},  // > -> <
        {CmpStmt::Predicate::ICMP_ULT, CmpStmt::Predicate::ICMP_UGT},  // < -> >
        {CmpStmt::Predicate::ICMP_UGE, CmpStmt::Predicate::ICMP_ULE},  // >= -> <=
        {CmpStmt::Predicate::ICMP_SGT, CmpStmt::Predicate::ICMP_SLT},  // > -> <
        {CmpStmt::Predicate::ICMP_SLT, CmpStmt::Predicate::ICMP_SGT},  // < -> >
        {CmpStmt::Predicate::ICMP_SGE, CmpStmt::Predicate::ICMP_SLE},  // >= -> <=
    };

};
}