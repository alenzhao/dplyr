#ifndef dplyr_GroupedCallProxy_H
#define dplyr_GroupedCallProxy_H

#include <dplyr/get_column.h>

#include <dplyr/Hybrid.h>

#include <dplyr/Result/CallElementProxy.h>
#include <dplyr/Result/LazyGroupedSubsets.h>
#include <dplyr/Result/ILazySubsets.h>
#include <dplyr/Result/GroupedHybridCall.h>

namespace dplyr {

  template <typename Data = GroupedDataFrame, typename Subsets = LazyGroupedSubsets>
  class GroupedCallProxy {
  public:
    typedef GroupedHybridCall<Subsets> HybridCall;

    GroupedCallProxy(Call call_, const Subsets& subsets_, const Environment& env_) :
      call(call_), subsets(subsets_), proxies(), env(env_)
    {
      set_call(call);
    }

    GroupedCallProxy(const Rcpp::Call& call_, const Data& data_, const Environment& env_) :
      call(call_), subsets(data_), proxies(), env(env_)
    {
      set_call(call);
    }

    GroupedCallProxy(const Data& data_, const Environment& env_) :
      subsets(data_), proxies(), env(env_)
    {}

    GroupedCallProxy(const Data& data_) :
      subsets(data_), proxies()
    {}

    ~GroupedCallProxy() {}

    SEXP get(const SlicingIndex& indices) {
      subsets.clear();

      if (TYPEOF(call) == LANGSXP) {
        LOG_VERBOSE << "performing hybrid evaluation";
        HybridCall hybrid_eval(call, indices, subsets, env);
        return hybrid_eval.eval();
      } else if (TYPEOF(call) == SYMSXP) {
        if (subsets.count(call)) {
          return subsets.get(call, indices);
        }
        return env.find(CHAR(PRINTNAME(call)));
      } else {
        // all other types that evaluate to themselves
        return call;
      }
    }

    SEXP eval() {
      return get(NaturalSlicingIndex(subsets.nrows()));
    }

    void set_call(SEXP call_) {
      proxies.clear();
      call = call_;
      if (TYPEOF(call) == LANGSXP) traverse_call(call);
    }

    void input(Symbol name, SEXP x) {
      subsets.input(name, x);
    }

    inline int nsubsets() {
      return subsets.size();
    }

    inline SEXP get_variable(Rcpp::String name) const {
      return subsets.get_variable(Rf_installChar(name.get_sexp()));
    }

    inline bool is_constant() const {
      return TYPEOF(call) != LANGSXP && Rf_length(call) == 1;
    }

    inline SEXP get_call() const {
      return call;
    }

    inline bool has_variable(SEXP symbol) const {
      return subsets.count(symbol);
    }

    inline void set_env(SEXP env_) {
      env = env_;
    }

  private:
    void traverse_call(SEXP obj) {
      if (TYPEOF(obj) == LANGSXP && CAR(obj) == Rf_install("local")) return;

      if (TYPEOF(obj) == LANGSXP && CAR(obj) == Rf_install("global")) {
        SEXP symb = CADR(obj);
        if (TYPEOF(symb) != SYMSXP) stop("global only handles symbols");
        SEXP res = env.find(CHAR(PRINTNAME(symb)));
        call = res;
        return;
      }

      if (TYPEOF(obj) == LANGSXP && CAR(obj) == Rf_install("column")) {
        call = get_column(CADR(obj), env, subsets);
        return;
      }

      if (! Rf_isNull(obj)) {
        SEXP head = CAR(obj);

        switch (TYPEOF(head)) {
        case LANGSXP:
          if (CAR(head) == Rf_install("global")) {
            SEXP symb = CADR(head);
            if (TYPEOF(symb) != SYMSXP) stop("global only handles symbols");

            SEXP res  = env.find(CHAR(PRINTNAME(symb)));

            SETCAR(obj, res);
            SET_TYPEOF(obj, LISTSXP);
            break;
          }
          if (CAR(head) == Rf_install("column")) {
            Symbol column = get_column(CADR(head), env, subsets);
            SETCAR(obj, column);
            head = CAR(obj);
            proxies.push_back(CallElementProxy(head, obj));
            break;
          }

          if (CAR(head) == Rf_install("~")) break;
          if (CAR(head) == Rf_install("order_by")) break;
          if (CAR(head) == Rf_install("function")) break;
          if (CAR(head) == Rf_install("local")) return;
          if (CAR(head) == Rf_install("<-")) {
            stop("assignments are forbidden");
          }

          if (Rf_length(head) == 3) {
            SEXP symb = CAR(head);
            if (symb == R_DollarSymbol /* "$" */ || symb == Rf_install("@") || symb == Rf_install("::") || symb == Rf_install(":::")) {

              // for things like : foo( bar = bling )$bla
              // so that `foo( bar = bling )` gets processed
              if (TYPEOF(CADR(head)) == LANGSXP) {
                traverse_call(CDR(head));
              }

              // deal with foo$bar( bla = boom )
              if (TYPEOF(CADDR(head)) == LANGSXP) {
                traverse_call(CDDR(head));
              }

              break;
            }
          }
          traverse_call(CDR(head));
          break;

        case LISTSXP:
          traverse_call(head);
          traverse_call(CDR(head));
          break;

        case SYMSXP:
          if (TYPEOF(obj) != LANGSXP) {
            if (! subsets.count(head)) {

              // in the Environment -> resolve
              try {
                if (head == R_MissingArg) break;
                if (head == Rf_install(".")) break;

                Shield<SEXP> x(env.find(CHAR(PRINTNAME(head))));
                SETCAR(obj, x);
              } catch (...) {
                // when the binding is not found in the environment
                // e.g. summary(mod)$r.squared
                // the "r.squared" is not in the env
              }
            } else {
              // in the data frame
              proxies.push_back(CallElementProxy(head, obj));
            }
          }
          break;
        }

        traverse_call(CDR(obj));
      }
    }

    Rcpp::Call call;
    Subsets subsets;
    std::vector<CallElementProxy> proxies;
    Environment env;

  };

}

#endif
