/*++
Copyright (c) 2013 Microsoft Corporation

Module Name:

    realclosure.cpp

Abstract:

    Package for computing with elements of the realclosure of a field containing
       - all rationals
       - extended with computable transcendental real numbers (e.g., pi and e)
       - infinitesimals

Author:

    Leonardo (leonardo) 2013-01-02

Notes:

--*/
#include"realclosure.h"
#include"array.h"
#include"mpbq.h"
#include"interval_def.h"
#include"obj_ref.h"
#include"ref_vector.h"
#include"ref_buffer.h"
#include"cooperate.h"

#ifndef REALCLOSURE_INI_BUFFER_SIZE
#define REALCLOSURE_INI_BUFFER_SIZE 32
#endif

#ifndef REALCLOSURE_INI_SEQ_SIZE
#define REALCLOSURE_INI_SEQ_SIZE 256
#endif

#ifndef REALCLOSURE_INI_DIV_PRECISION
#define REALCLOSURE_INI_DIV_PRECISION 24
#endif

namespace realclosure {
    
    // ---------------------------------
    //
    // Intervals with binary rational endpoints
    //
    // ---------------------------------

    struct mpbq_config {

        struct numeral_manager : public mpbq_manager {
            // division is not precise
            static bool precise() { return false; }
            static bool field() { return true; }
            unsigned m_div_precision;
            bool     m_to_plus_inf;
            
            numeral_manager(unsynch_mpq_manager & qm):mpbq_manager(qm), m_div_precision(REALCLOSURE_INI_DIV_PRECISION), m_to_plus_inf(true) {
            }

            void div(mpbq const & a, mpbq const & b, mpbq & c) {
                approx_div(a, b, c, m_div_precision, m_to_plus_inf);
            }

            void inv(mpbq & a) {
                mpbq one(1);
                scoped_mpbq r(*this);
                approx_div(one, a, r, m_div_precision, m_to_plus_inf);
                swap(a, r);
            }
        };

        typedef mpbq         numeral;
        numeral_manager &    m_manager;

        struct interval {
            numeral   m_lower;
            numeral   m_upper;
            unsigned  m_lower_inf:1;
            unsigned  m_upper_inf:1;
            unsigned  m_lower_open:1;
            unsigned  m_upper_open:1;
            interval():m_lower_inf(true), m_upper_inf(true), m_lower_open(true), m_upper_open(true) {}
            interval(numeral & l, numeral & u):m_lower_inf(false), m_upper_inf(false), m_lower_open(true), m_upper_open(true) {
                swap(m_lower, l);
                swap(m_upper, u);
            }
            numeral & lower() { return m_lower; }
            numeral & upper() { return m_upper; }
            void set_lower_is_inf(bool f) { m_lower_inf = f; }
            void set_upper_is_inf(bool f) { m_upper_inf = f; }
            void set_lower_is_open(bool f) { m_lower_open = f; }
            void set_upper_is_open(bool f) { m_upper_open = f; }
            numeral const & lower() const { return m_lower; }
            numeral const & upper() const { return m_upper; }
            bool lower_is_inf() const { return m_lower_inf; }
            bool upper_is_inf() const { return m_upper_inf; }
            bool lower_is_open() const { return m_lower_open; }
            bool upper_is_open() const { return m_upper_open; }
        };

        void set_rounding(bool to_plus_inf) { m_manager.m_to_plus_inf = to_plus_inf; }
        void round_to_minus_inf() { set_rounding(false); }
        void round_to_plus_inf() { set_rounding(true); }
        
        // Getters
        numeral const & lower(interval const & a) const { return a.m_lower; }
        numeral const & upper(interval const & a) const { return a.m_upper; }
        numeral & lower(interval & a) { return a.m_lower; }
        numeral & upper(interval & a) { return a.m_upper; }
        bool lower_is_open(interval const & a) const { return a.m_lower_open; }
        bool upper_is_open(interval const & a) const { return a.m_upper_open; }
        bool lower_is_inf(interval const & a) const { return a.m_lower_inf; }
        bool upper_is_inf(interval const & a) const { return a.m_upper_inf; }
        
        // Setters
        void set_lower(interval & a, numeral const & n) { m_manager.set(a.m_lower, n); }
        void set_upper(interval & a, numeral const & n) { m_manager.set(a.m_upper, n); }
        void set_lower_is_open(interval & a, bool v) { a.m_lower_open = v; }
        void set_upper_is_open(interval & a, bool v) { a.m_upper_open = v; }
        void set_lower_is_inf(interval & a, bool v) { a.m_lower_inf = v; }
        void set_upper_is_inf(interval & a, bool v) { a.m_upper_inf = v; }
        
        // Reference to numeral manager
        numeral_manager & m() const { return m_manager; }
        
        mpbq_config(numeral_manager & m):m_manager(m) {}
    };
    
    typedef interval_manager<mpbq_config> mpbqi_manager;
    typedef mpbqi_manager::interval       mpbqi;

    void swap(mpbqi & a, mpbqi & b) {
        swap(a.m_lower, b.m_lower);
        swap(a.m_upper, b.m_upper);
        unsigned tmp;
        tmp = a.m_lower_inf; a.m_lower_inf = b.m_lower_inf; b.m_lower_inf = tmp;
        tmp = a.m_upper_inf; a.m_upper_inf = b.m_upper_inf; b.m_upper_inf = tmp;
    }

    // ---------------------------------
    //
    // Values are represented as
    //   - arbitrary precision rationals (mpq)
    //   - rational functions on field extensions
    //
    // ---------------------------------

    struct value {
        unsigned m_ref_count;  //!< Reference counter
        bool     m_rational;   //!< True if the value is represented as an abitrary precision rational value.
        mpbqi    m_interval;   //!< approximation as an interval with binary rational end-points
        // When performing an operation OP, we may have to make the width (upper - lower) of m_interval very small.
        // The precision (i.e., a small interval) needed for executing OP is usually unnecessary for subsequent operations,
        // This unnecessary precision will only slowdown the subsequent operations that do not need it.
        // To cope with this issue, we cache the value m_interval in m_old_interval whenever the width of m_interval is below
        // a give threshold. Then, after finishing OP, we restore the old_interval.
        mpbqi *  m_old_interval; 
        value(bool rat):m_ref_count(0), m_rational(rat), m_old_interval(0) {}
        bool is_rational() const { return m_rational; }
        mpbqi const & interval() const { return m_interval; }
        mpbqi & interval() { return m_interval; }
    };

    struct rational_value : public value {
        mpq      m_value;
        rational_value():value(true) {}
    };

    typedef ptr_array<value> polynomial;
    
    struct extension;
    bool rank_lt(extension * r1, extension * r2);

    struct rational_function_value : public value {
        polynomial   m_numerator;
        polynomial   m_denominator;
        extension *  m_ext;
        bool         m_real; //!< True if the polynomial expression does not depend on infinitesimal values.
        rational_function_value(extension * ext):value(false), m_ext(ext), m_real(false) {}
        
        polynomial const & num() const { return m_numerator; }
        polynomial & num() { return m_numerator; }
        polynomial const & den() const { return m_denominator; }
        polynomial & den() { return m_denominator; }

        extension * ext() const { return m_ext; }
        bool is_real() const { return m_real; }
        void set_real(bool f) { m_real = f; }
    };

    typedef ptr_vector<value> value_vector;

    // ---------------------------------
    //
    // Field Extensions
    //
    // ---------------------------------
    
    typedef int sign;

    typedef std::pair<polynomial, sign> p2s;

    typedef sarray<p2s> signs;

    struct extension {
        enum kind {
            TRANSCENDENTAL = 0,
            INFINITESIMAL  = 1,
            ALGEBRAIC      = 2
        };

        unsigned m_ref_count;
        unsigned m_kind:2;
        unsigned m_idx:30;
        mpbqi    m_interval;

        extension(kind k, unsigned idx):m_ref_count(0), m_kind(k), m_idx(idx) {}

        unsigned idx() const { return m_idx; }
        kind knd() const { return static_cast<kind>(m_kind); }

        bool is_algebraic() const { return knd() == ALGEBRAIC; }
        bool is_infinitesimal() const { return knd() == INFINITESIMAL; }
        bool is_transcendental() const { return knd() == TRANSCENDENTAL; }

        mpbqi const & interval() const { return m_interval; }
        mpbqi & interval() { return m_interval; }
    };

    bool rank_lt(extension * r1, extension * r2) {
        return r1->knd() < r2->knd() || (r1->knd() == r2->knd() && r1->idx() < r2->idx()); 
    }

    bool rank_eq(extension * r1, extension * r2) {
        return r1->knd() == r2->knd() && r1->idx() == r2->idx(); 
    }

    struct rank_lt_proc {
        bool operator()(extension * r1, extension * r2) const {
            return rank_lt(r1, r2);
        }
    };

    struct algebraic : public extension {
        polynomial   m_p;
        signs        m_signs;
        bool         m_real;  //!< True if the polynomial p does not depend on infinitesimal extensions.

        algebraic(unsigned idx):extension(ALGEBRAIC, idx), m_real(false) {}

        polynomial const & p() const { return m_p; }
        signs const & s() const { return m_signs; }
        bool is_real() const { return m_real; }
    };

    struct transcendental : public extension {
        symbol        m_name;
        unsigned      m_k;
        mk_interval & m_proc;
        
        transcendental(unsigned idx, symbol const & n, mk_interval & p):extension(TRANSCENDENTAL, idx), m_name(n), m_k(0), m_proc(p) {}

        void display(std::ostream & out) const {
            out << m_name;
        }
    };
    
    struct infinitesimal : public extension {
        symbol        m_name;
 
        infinitesimal(unsigned idx, symbol const & n):extension(INFINITESIMAL, idx), m_name(n) {}

        void display(std::ostream & out) const {
            if (m_name.is_numerical())
                out << "eps!" << m_name.get_num();
            else
                out << m_name;
        }
    };

    // ---------------------------------
    //
    // Predefined transcendental mk_interval procs
    //
    // ---------------------------------
    
    struct mk_pi_interval : public mk_interval {
        virtual void operator()(unsigned k, mpqi_manager & im, mpqi_manager::interval & r) {
            im.pi(k, r);
        }
    };

    struct mk_e_interval : public mk_interval {
        virtual void operator()(unsigned k, mpqi_manager & im, mpqi_manager::interval & r) {
            im.e(k, r);
        }
    };

    // ---------------------------------
    //
    // Manager
    //
    // ---------------------------------

    struct manager::imp {
        typedef ref_vector<value, imp> value_ref_vector;
        typedef ref_buffer<value, imp, REALCLOSURE_INI_BUFFER_SIZE> value_ref_buffer;
        typedef obj_ref<value, imp>    value_ref;
        typedef _scoped_interval<mpqi_manager>  scoped_mpqi;
        typedef _scoped_interval<mpbqi_manager> scoped_mpbqi;
         
        small_object_allocator *       m_allocator;
        bool                           m_own_allocator;
        unsynch_mpq_manager &          m_qm;
        mpbq_config::numeral_manager   m_bqm;
        mpqi_manager                   m_qim;
        mpbqi_manager                  m_bqim;
        ptr_vector<extension>          m_extensions[3];
        value *                        m_one;
        mk_pi_interval                 m_mk_pi_interval;
        value *                        m_pi;
        mk_e_interval                  m_mk_e_interval;
        value *                        m_e;
        ptr_vector<value>              m_to_restore; //!< Set of values v s.t. v->m_old_interval != 0
        
        // Parameters
        unsigned                       m_ini_precision; //!< initial precision for transcendentals, infinitesimals, etc.
        int                            m_min_magnitude;
        unsigned                       m_inf_precision; //!< 2^m_inf_precision is used as the lower bound of oo and -2^m_inf_precision is used as the upper_bound of -oo
        scoped_mpbq                    m_plus_inf_approx; // lower bound for binary rational intervals used to approximate an infinite positive value
        scoped_mpbq                    m_minus_inf_approx; // upper bound for binary rational intervals used to approximate an infinite negative value

        volatile bool                  m_cancel;

        struct scoped_polynomial_seq {
            typedef ref_buffer<value, imp, REALCLOSURE_INI_SEQ_SIZE> value_seq;
            value_seq          m_seq_coeffs;
            sbuffer<unsigned>  m_begins;     // start position (in m_seq_coeffs) of each polynomial in the sequence
            sbuffer<unsigned>  m_szs;        // size of each polynomial in the sequence 
        public:
            scoped_polynomial_seq(imp & m):m_seq_coeffs(m) {}
            
            /**
               \brief Add a new polynomial to the sequence.
               The contents of p is erased.
            */
            void push(unsigned sz, value * const * p) {
                m_begins.push_back(m_seq_coeffs.size());
                m_szs.push_back(sz);
                m_seq_coeffs.append(sz, p);
            }

            /**
               \brief Return the number of polynomials in the sequence.
            */
            unsigned size() const { return m_szs.size(); }
            
            /**
               \brief Return the vector of coefficients for the i-th polynomial in the sequence.
            */
            value * const * coeffs(unsigned i) const { 
                return m_seq_coeffs.c_ptr() + m_begins[i]; 
            }
        
            /**
               \brief Return the size of the i-th polynomial in the sequence.
            */
            unsigned size(unsigned i) const { return m_szs[i]; }

            void reset() {
                m_seq_coeffs.reset();
                m_begins.reset();
                m_szs.reset();
            }
        };
        
        imp(unsynch_mpq_manager & qm, params_ref const & p, small_object_allocator * a):
            m_allocator(a == 0 ? alloc(small_object_allocator, "realclosure") : a),
            m_own_allocator(a == 0),
            m_qm(qm),
            m_bqm(m_qm),
            m_qim(m_qm),
            m_bqim(m_bqm),
            m_plus_inf_approx(m_bqm),
            m_minus_inf_approx(m_bqm) {
            mpq one(1);
            m_one = mk_rational(one);
            inc_ref(m_one);
            m_pi = 0;
            m_e  = 0;
            m_cancel = false;
            
            updt_params(p);
        }

        ~imp() {
            restore_saved_intervals(); // to free memory
            dec_ref(m_one);
            dec_ref(m_pi);
            dec_ref(m_e);
            if (m_own_allocator)
                dealloc(m_allocator);
        }

        unsynch_mpq_manager & qm() const { return m_qm; }
        mpbq_config::numeral_manager & bqm() { return m_bqm; }
        mpqi_manager & qim() { return m_qim; }
        mpbqi_manager & bqim() { return m_bqim; }
        mpbqi_manager const & bqim() const { return m_bqim; }
        small_object_allocator & allocator() { return *m_allocator; }

        void checkpoint() {
            if (m_cancel)
                throw exception("canceled");
            cooperate("rcf");
        }

        value * one() const {
            return m_one;
        }

        /**
           \brief Return the magnitude of the given interval.
           The magnitude is an approximation of the size of the interval.
        */
        int magnitude(mpbq const & l, mpbq const & u) {
            SASSERT(bqm().ge(u, l));
            scoped_mpbq w(bqm());
            bqm().sub(u, l, w);
            if (bqm().is_zero(w))
                return INT_MIN;
            SASSERT(bqm().is_pos(w));
            return bqm().magnitude_ub(w);
        }
        
        /**
           \brief Return the magnitude of the given interval.
           The magnitude is an approximation of the size of the interval.
        */
        int magnitude(mpbqi const & i) {
            if (i.lower_is_inf() || i.upper_is_inf())
                return INT_MAX;
            else
                return magnitude(i.lower(), i.upper());
        }

        /**
           \brief Return the magnitude of the given interval.
           The magnitude is an approximation of the size of the interval.
        */
        int magnitude(mpq const & l, mpq const & u) {
            SASSERT(qm().ge(u, l));
            scoped_mpq w(qm());
            qm().sub(u, l, w);
            if (qm().is_zero(w))
                return INT_MIN;
            SASSERT(qm().is_pos(w));
            return static_cast<int>(qm().log2(w.get().numerator())) + 1 - static_cast<int>(qm().log2(w.get().denominator()));
        }

        int magnitude(scoped_mpqi const & i) {
            SASSERT(!i->m_lower_inf && !i->m_upper_inf);
            return magnitude(i->m_lower, i->m_upper);
        }
        
        /**
           \brief Return true if the magnitude of the given interval is less than the parameter m_min_magnitude
        */
        bool too_small(mpbqi const & i) {
            return magnitude(i) < m_min_magnitude;
        }

#define SMALL_UNSIGNED 1 << 16
        static unsigned inc_precision(unsigned prec, unsigned inc) {
            if (prec < SMALL_UNSIGNED)
                return prec + inc;
            else
                return prec;
        }

        struct scoped_set_div_precision {
            mpbq_config::numeral_manager & m_bqm;
            unsigned                       m_old_precision;
            scoped_set_div_precision(mpbq_config::numeral_manager & bqm, unsigned prec):m_bqm(bqm) {
                m_old_precision = m_bqm.m_div_precision;
                m_bqm.m_div_precision = prec;
            }
            ~scoped_set_div_precision() {
                m_bqm.m_div_precision = m_old_precision;
            }
        };

        /**
           \brief c <- a/b with precision prec.
        */
        void div(mpbqi const & a, mpbqi const & b, unsigned prec, mpbqi & c) {
            scoped_set_div_precision set(bqm(), prec);
            bqim().div(a, b, c);
        }

        /**
           \brief Save the current interval (i.e., approximation) of the given value.
        */
        void save_interval(value * v) {
            if (v->m_old_interval != 0)
                return; // interval was already saved.
            m_to_restore.push_back(v);
            inc_ref(v);
            v->m_old_interval = new (allocator()) mpbqi();
            set_interval(*(v->m_old_interval), v->m_interval);
        }

        /**
           \brief Save the current interval (i.e., approximation) of the given value IF it is too small (i.e., too_small(v) == True).
        */
        void save_interval_if_too_small(value * v) {
            if (too_small(v->m_interval))
                save_interval(v);
        }

        /**
           \brief Restore the saved intervals (approximations) of RCF values.
        */
        void restore_saved_intervals() {
            unsigned sz = m_to_restore.size();
            for (unsigned i = 0; i < sz; i++) {
                value * v = m_to_restore[i];
                set_interval(v->m_interval, *(v->m_old_interval));
                bqim().del(*(v->m_old_interval));
                allocator().deallocate(sizeof(mpbqi), v->m_old_interval);
                v->m_old_interval = 0;
                dec_ref(v);
            }
            m_to_restore.reset();
        }

        void cleanup(extension::kind k) {
            ptr_vector<extension> & exts = m_extensions[k];
            // keep removing unused slots
            while (!exts.empty() && exts.back() == 0) {
                exts.pop_back();
            }
        }

        unsigned next_transcendental_idx() {
            cleanup(extension::TRANSCENDENTAL);
            return m_extensions[extension::TRANSCENDENTAL].size();
        }

        unsigned next_infinitesimal_idx() {
            cleanup(extension::INFINITESIMAL);
            return m_extensions[extension::INFINITESIMAL].size();
        }
        
        void set_cancel(bool f) {
            m_cancel = f;
        }
        
        void updt_params(params_ref const & p) {
            m_ini_precision  = p.get_uint("initial_precision", 24);
            m_inf_precision  = p.get_uint("inf_precision", 24);
            m_min_magnitude  = -static_cast<int>(p.get_uint("min_mag", 64));
            bqm().power(mpbq(2), m_inf_precision, m_plus_inf_approx);
            bqm().set(m_minus_inf_approx, m_plus_inf_approx);
            bqm().neg(m_minus_inf_approx);
        }

        /**
           \brief Reset the given polynomial.
           That is, after the call p is the 0 polynomial.
        */
        void reset_p(polynomial & p) {
            dec_ref(p.size(), p.c_ptr());
            p.finalize(allocator());
        }

        void del_rational(rational_value * v) {
            bqim().del(v->m_interval);
            qm().del(v->m_value);
            allocator().deallocate(sizeof(rational_value), v);
        }

        void del_rational_function(rational_function_value * v) {
            bqim().del(v->m_interval);
            reset_p(v->num());
            reset_p(v->den());
            dec_ref_ext(v->ext());
            allocator().deallocate(sizeof(rational_function_value), v);
        }

        void del_value(value * v) {
            if (v->is_rational())
                del_rational(static_cast<rational_value*>(v));
            else
                del_rational_function(static_cast<rational_function_value*>(v));
        }

        void del_algebraic(algebraic * a) {
            reset_p(a->m_p);
            bqim().del(a->m_interval);
            unsigned sz = a->m_signs.size();
            for (unsigned i = 0; i < sz; i++) {
                reset_p(a->m_signs[i].first);
            }
            allocator().deallocate(sizeof(algebraic), a);
        }

        void del_transcendental(transcendental * t) {
            allocator().deallocate(sizeof(transcendental), t);
        }
        
        void del_infinitesimal(infinitesimal * i) {
            allocator().deallocate(sizeof(infinitesimal), i);
        }

        void inc_ref_ext(extension * ext) {
            SASSERT(ext != 0);
            ext->m_ref_count++;
        }

        void dec_ref_ext(extension * ext) {
            SASSERT(m_extensions[ext->knd()][ext->idx()] == ext);
            SASSERT(ext->m_ref_count > 0);
            ext->m_ref_count--;
            if (ext->m_ref_count == 0) {
                m_extensions[ext->knd()][ext->idx()] = 0;
                switch (ext->knd()) {
                case extension::TRANSCENDENTAL: del_transcendental(static_cast<transcendental*>(ext)); break;
                case extension::INFINITESIMAL:  del_infinitesimal(static_cast<infinitesimal*>(ext)); break;
                case extension::ALGEBRAIC:      del_algebraic(static_cast<algebraic*>(ext)); break;
                }
            }
        }

        void inc_ref(value * v) {
            if (v)
                v->m_ref_count++;
        }

        void inc_ref(unsigned sz, value * const * p) {
            for (unsigned i = 0; i < sz; i++)
                inc_ref(p[i]);
        }

        void dec_ref(value * v) {
            if (v) {
                SASSERT(v->m_ref_count > 0);
                v->m_ref_count--;
                if (v->m_ref_count == 0)
                    del_value(v);
            }
        }

        void dec_ref(unsigned sz, value * const * p) {
            for (unsigned i = 0; i < sz; i++)
                dec_ref(p[i]);
        }

        void del(numeral & a) {
            dec_ref(a.m_value);
            a.m_value = 0;
        }

        /**
           \brief Return true if the given interval is smaller than 1/2^k
        */
        bool check_precision(mpbqi const & interval, unsigned k) {
            if (interval.lower_is_inf() || interval.upper_is_inf())
                return false;
            scoped_mpbq w(bqm());
            bqm().sub(interval.upper(), interval.lower(), w);
            return bqm().lt_1div2k(w, k);
        }

        /**
           \brief Return true if v is zero.
        */
        static bool is_zero(value * v) {
            return v == 0;
        }

        /**
           \brief Return true if v is represented using a nonzero arbitrary precision rational value.
        */
        static bool is_nz_rational(value * v) { 
            SASSERT(v != 0);
            return v->is_rational(); 
        }

        /**
           \brief Return true if v is represented as rational value one.
        */
        bool is_rational_one(value * v) const {
            return !is_zero(v) && is_nz_rational(v) && qm().is_one(to_mpq(v));
        }

        /**
           \brief Return true if v is represented as rational value minus one.
        */
        bool is_rational_minus_one(value * v) const {
            return !is_zero(v) && is_nz_rational(v) && qm().is_minus_one(to_mpq(v));
        }

        /**
           \brief Return true if v is the value one;
         */
        bool is_one(value * v) const {
            if (is_rational_one(v))
                return true;
            // TODO: check if v is equal to one.
            return false;
        }

        /**
           \brief Return true if p is the constant polynomial where the coefficient is 
           the rational value 1.

           \remark This is NOT checking whether p is actually equal to 1.
           That is, it is just checking the representation.
        */
        bool is_rational_one(polynomial const & p) const {
            return p.size() == 1 && is_rational_one(p[0]);
        }
        bool is_rational_one(value_ref_buffer const & p) const {
            return p.size() == 1 && is_rational_one(p[0]);
        }

        template<typename T>
        bool is_one(polynomial const & p) const {
            return p.size() == 1 && is_one(p[0]);
        }

        /**
           \brief Return true if v is a represented as a rational function of the set of field extensions.
        */
        static bool is_rational_function(value * v) {
            SASSERT(v != 0);
            return !(v->is_rational());
        }
        
        static rational_value * to_nz_rational(value * v) { 
            SASSERT(is_nz_rational(v)); 
            return static_cast<rational_value*>(v); 
        }
        
        static rational_function_value * to_rational_function(value * v) { 
            SASSERT(!is_nz_rational(v)); 
            return static_cast<rational_function_value*>(v); 
        }

        static bool is_zero(numeral const & a) {
            return is_zero(a.m_value);
        }

        static bool is_nz_rational(numeral const & a) {
            SASSERT(!is_zero(a));
            return is_nz_rational(a.m_value);
        }

        /**
           \brief Return true if v is not a shared value. That is, we can perform
           destructive updates.
        */
        static bool is_unique(value * v) {
            SASSERT(v);
            return v->m_ref_count <= 1;
        }

        static bool is_unique(numeral const & a) {
            return is_unique(a.m_value);
        }

        static bool is_unique_nz_rational(value * v) {
            return is_nz_rational(v) && is_unique(v);
        }

        static bool is_unique_nz_rational(numeral const & a) {
            return is_unique_nz_rational(a.m_value);
        }

        static rational_value * to_nz_rational(numeral const & a) {
            SASSERT(is_nz_rational(a));
            return to_nz_rational(a.m_value);
        }

        static bool is_rational_function(numeral const & a) {
            return is_rational_function(a.m_value);
        }

        static rational_function_value * to_rational_function(numeral const & a) {
            SASSERT(is_rational_function(a));
            return to_rational_function(a.m_value);
        }

        static mpq & to_mpq(value * v) {
            SASSERT(is_nz_rational(v));
            return to_nz_rational(v)->m_value;
        }

        static mpq & to_mpq(numeral const & a) {
            SASSERT(is_nz_rational(a));
            return to_nz_rational(a)->m_value;
        }

        static int compare_rank(value * a, value * b) {
            SASSERT(a); SASSERT(b);
            if (is_nz_rational(a))
                return is_nz_rational(b) ? 0 : -1;
            else if (is_nz_rational(b)) {
                SASSERT(is_rational_function(a));
                return 1;
            }
            else if (rank_eq(to_rational_function(a)->ext(), to_rational_function(b)->ext()))
                return 0;
            else 
                return rank_lt(to_rational_function(a)->ext(), to_rational_function(b)->ext()) ? -1 : 1;
        }

        static transcendental * to_transcendental(extension * ext) {
            SASSERT(ext->is_transcendental());
            return static_cast<transcendental*>(ext);
        }

        static infinitesimal * to_infinitesimal(extension * ext) {
            SASSERT(ext->is_infinitesimal());
            return static_cast<infinitesimal*>(ext);
        }

        static algebraic * to_algebraic(extension * ext) {
            SASSERT(ext->is_algebraic());
            return static_cast<algebraic*>(ext);
        }
        
        /**
           \brief Return True if the given extension is a Real value.
           The result is approximate for algebraic extensions.
             For algebraic extensions, we have
               - true result is always correct (i.e., the extension is really a real value)
               - false result is approximate (i.e., the extension may be a real value although it is a root of a polynomial that contains non-real coefficients)
                   Example: Assume eps is an infinitesimal, and pi is 3.14... . 
                   Assume also that ext is the unique root between (3, 4) of the following polynomial:
                          x^2 - (pi + eps)*x + pi*ext 
                   Thus, x is pi, but the system will return false, since its defining polynomial has infinitesimal
                   coefficients. In the future, to make everything precise, we should be able to factor the polynomial
                   above as 
                          (x - eps)*(x - pi)
                   and then detect that x is actually the root of (x - pi).
        */
        bool is_real(extension * ext) {
            switch (ext->knd()) {
            case extension::TRANSCENDENTAL: return true;
            case extension::INFINITESIMAL:  return false;
            case extension::ALGEBRAIC:      return to_algebraic(ext)->is_real();
            default:
                UNREACHABLE();
                return false;
            }
        }

        /**
           \brief Return true if v is definitely a real value.
        */
        bool is_real(value * v) {
            if (is_zero(v) || is_nz_rational(v))
                return true;
            else 
                return to_rational_function(v)->is_real();
        }

        bool is_real(unsigned sz, value * const * p) {
            for (unsigned i = 0; i < sz; i++)
                if (!is_real(p[i]))
                    return false;
            return true;
        }

        /**
           \brief Set the polynomial p with the given coefficients as[0], ..., as[n-1]
        */
        void set_p(polynomial & p, unsigned n, value * const * as) {
            SASSERT(n > 0);
            SASSERT(!is_zero(as[n - 1]));
            reset_p(p);
            p.set(allocator(), n, as);
            inc_ref(n, as);
        }

        /**
           \brief Return true if a is an open interval.
        */
        static bool is_open_interval(mpbqi const & a) {
            return a.lower_is_inf() && a.upper_is_inf();
        }

        /**
           \brief Return true if the interval contains zero.
        */
        bool contains_zero(mpbqi const & a) const {
            return bqim().contains_zero(a);
        }

        /**
           \brief Set the lower bound of the given interval.
        */
        void set_lower_core(mpbqi & a, mpbq const & k, bool open, bool inf) {
            bqm().set(a.lower(), k);
            a.set_lower_is_open(open);
            a.set_lower_is_inf(inf);
        }

        /**
           \brief a.lower <- k
        */
        void set_lower(mpbqi & a, mpbq const & k, bool open = true) {
            set_lower_core(a, k, open, false);
        }

        /**
           \brief a.lower <- -oo
        */
        void set_lower_inf(mpbqi & a) {
            bqm().reset(a.lower());
            a.set_lower_is_open(true);
            a.set_lower_is_inf(true);
        }

        /**
           \brief Set the upper bound of the given interval.
        */
        void set_upper_core(mpbqi & a, mpbq const & k, bool open, bool inf) {
            bqm().set(a.upper(), k);
            a.set_upper_is_open(open);
            a.set_upper_is_inf(inf);
        }

        /**
           \brief a.upper <- k
        */
        void set_upper(mpbqi & a, mpbq const & k, bool open = true) {
            set_upper_core(a, k, open, false);
        }

        /**
           \brief a.upper <- oo
        */
        void set_upper_inf(mpbqi & a) {
            bqm().reset(a.upper());
            a.set_upper_is_open(true);
            a.set_upper_is_inf(true);
        }

        /**
           \brief a <- b
        */
        void set_interval(mpbqi & a, mpbqi const & b) {
            set_lower_core(a, b.lower(), b.lower_is_open(), b.lower_is_inf());
            set_upper_core(a, b.upper(), b.upper_is_open(), b.upper_is_inf());
        }

        /**
           \brief Make a rational_function_value using the given extension, numerator and denominator.
           This method does not set the interval. It remains (-oo, oo)
        */
        rational_function_value * mk_rational_function_value_core(extension * ext, unsigned num_sz, value * const * num, unsigned den_sz, value * const * den) {
            rational_function_value * r = alloc(rational_function_value, ext);
            inc_ref_ext(ext);
            set_p(r->num(), num_sz, num);
            set_p(r->den(), den_sz, den);
            r->set_real(is_real(ext) && is_real(num_sz, num) && is_real(den_sz, den));
            return r;
        }         

        /**
           \brief Create a value using the given extension.
        */
        rational_function_value * mk_rational_function_value(extension * ext) {
            value * num[2] = { 0, one() };
            value * den[1] = { one() };
            rational_function_value * v = mk_rational_function_value_core(ext, 2, num, 1, den);
            set_interval(v->interval(), ext->interval());
            return v;
        }

        /**
           \brief Create a new infinitesimal.
        */
        void mk_infinitesimal(symbol const & n, numeral & r) {
            unsigned idx = next_infinitesimal_idx();
            infinitesimal * eps = alloc(infinitesimal, idx, n);
            m_extensions[extension::INFINITESIMAL].push_back(eps);

            set_lower(eps->interval(), mpbq(0));
            set_upper(eps->interval(), mpbq(1, m_ini_precision));
            
            set(r, mk_rational_function_value(eps));

            SASSERT(sign(r) > 0);
            SASSERT(!is_real(r));
        }

        void mk_infinitesimal(char const * n, numeral & r) {
            mk_infinitesimal(symbol(n), r);
        }

        void mk_infinitesimal(numeral & r) {
            mk_infinitesimal(symbol(next_infinitesimal_idx()), r);
        }

        void refine_transcendental_interval(transcendental * t) {
            scoped_mpqi i(qim());
            t->m_k++;
            t->m_proc(t->m_k, qim(), i);
            int m = magnitude(i);
            TRACE("rcf", 
                  tout << "refine_transcendental_interval rational: " << m << "\nrational interval: "; 
                  qim().display(tout, i); tout << std::endl;);
            unsigned k;
            if (m >= 0)
                k = m_ini_precision;
            else
                k = inc_precision(-m, 8);
            scoped_mpbq l(bqm());
            mpq_to_mpbqi(i->m_lower, t->interval(), k);
            // save lower
            bqm().set(l, t->interval().lower()); 
            mpq_to_mpbqi(i->m_upper, t->interval(), k);
            bqm().set(t->interval().lower(), l);
        }

        void refine_transcendental_interval(transcendental * t, unsigned prec) {
            while (!check_precision(t->interval(), prec)) {
                TRACE("rcf", tout << "refine_transcendental_interval: " << magnitude(t->interval()) << std::endl;);
                checkpoint();
                refine_transcendental_interval(t);
            }
        }

        void mk_transcendental(symbol const & n, mk_interval & proc, numeral & r) {
            unsigned idx = next_transcendental_idx();
            transcendental * t = alloc(transcendental, idx, n, proc);
            m_extensions[extension::TRANSCENDENTAL].push_back(t);
            
            while (contains_zero(t->interval())) {
                checkpoint();
                refine_transcendental_interval(t);
            }
            set(r, mk_rational_function_value(t));

            SASSERT(is_real(r));
        }
        
        void mk_transcendental(char const * p, mk_interval & proc, numeral & r) {
            mk_transcendental(symbol(p), proc, r);
        }

        void mk_transcendental(mk_interval & proc, numeral & r) {
            mk_transcendental(symbol(next_transcendental_idx()), proc, r);
        }

        void mk_pi(numeral & r) {
            if (m_pi) {
                set(r, m_pi);
            }
            else {
                mk_transcendental(symbol("pi"), m_mk_pi_interval, r);
                m_pi = r.m_value;
                inc_ref(m_pi);
            }
        }

        void mk_e(numeral & r) {
            if (m_e) {
                set(r, m_e);
            }
            else {
                mk_transcendental(symbol("e"), m_mk_e_interval, r);
                m_e = r.m_value;
                inc_ref(m_e);
            }
        }

        void isolate_roots(unsigned n, numeral const * as, numeral_vector & roots) {
            // TODO
        }

        void reset(numeral & a) {
            del(a);
            SASSERT(is_zero(a));
        }

        int sign(value * a) {
            if (is_zero(a))
                return 0;
            else if (is_nz_rational(a)) {
                return qm().is_pos(to_mpq(a)) ? 1 : -1;
            }
            else {
                SASSERT(!contains_zero(a->interval()));
                return bqim().is_P(a->interval()) ? 1 : -1;
            }
        }

        int sign(numeral const & a) {
            return sign(a.m_value);
        }
        
        bool is_int(numeral const & a) {
            if (is_zero(a))
                return true;
            else if (is_nz_rational(a)) 
                return qm().is_int(to_mpq(a));
            else {
                // TODO
                return false;
            }
        }

        bool is_real(value * v) const {
            if (is_zero(v) || is_nz_rational(v))
                return true;
            else
                return to_rational_function(v)->is_real();
        }
        
        bool is_real(numeral const & a) const {
            return is_real(a.m_value);
        }

        void mpq_to_mpbqi(mpq const & q, mpbqi & interval, unsigned k) {
            interval.set_lower_is_inf(false);
            interval.set_upper_is_inf(false);
            if (bqm().to_mpbq(q, interval.lower())) {
                bqm().set(interval.upper(), interval.lower());
                interval.set_lower_is_open(false);
                interval.set_upper_is_open(false);
            }
            else {
                bqm().set(interval.upper(), interval.lower());
                bqm().mul2(interval.upper());
                interval.set_lower_is_open(true);
                interval.set_upper_is_open(true);
                if (qm().is_neg(q)) {
                    ::swap(interval.lower(), interval.upper());
                }
                while (contains_zero(interval) || !check_precision(interval, k) || bqm().is_zero(interval.lower()) || bqm().is_zero(interval.upper())) {
                    checkpoint();
                    bqm().refine_lower(q, interval.lower(), interval.upper());
                    bqm().refine_upper(q, interval.lower(), interval.upper());
                }
            }
        }

        void initialize_rational_value_interval(value * a) {
            // For rational values, we only compute the binary intervals if needed.
            SASSERT(is_nz_rational(a));
            mpq_to_mpbqi(to_mpq(a), a->m_interval, m_ini_precision);
        }

        mpbqi & interval(value * a) const {
            SASSERT(a != 0);
            if (contains_zero(a->m_interval)) {
                SASSERT(is_nz_rational(a));
                const_cast<imp*>(this)->initialize_rational_value_interval(a);
            }
            return a->m_interval;
        }

        rational_value * mk_rational() {
            return new (allocator()) rational_value();
        }

        rational_value * mk_rational(mpq & v) {
            rational_value * r = mk_rational();
            ::swap(r->m_value, v);
            return r;
        }

        void reset_interval(value * a) {
            bqim().reset(a->m_interval);
        }

        template<typename T>
        void update_mpq_value(value * a, T & v) {
            SASSERT(is_nz_rational(a));
            qm().set(to_mpq(a), v);
            reset_interval(a);
        }

        template<typename T>
        void update_mpq_value(numeral & a, T & v) {
            update_mpq_value(a.m_value, v);
        }

        void set(numeral & a, int n) {
            if (n == 0) {
                reset(a);
                return;
            }
            
            del(a);
            a.m_value = mk_rational();
            inc_ref(a.m_value);
            update_mpq_value(a, n);
        }

        void set(numeral & a, mpz const & n) {
            if (qm().is_zero(n)) {
                reset(a);
                return;
            }

            del(a);
            a.m_value = mk_rational();
            inc_ref(a.m_value);
            update_mpq_value(a, n);
        }
        
        void set(numeral & a, mpq const & n) {
            if (qm().is_zero(n)) {
                reset(a);
                return;
            }
            del(a);
            a.m_value = mk_rational();
            inc_ref(a.m_value);
            update_mpq_value(a, n);
        }
        
        void set(numeral & a, numeral const & n) {
            inc_ref(n.m_value);
            dec_ref(a.m_value);
            a.m_value = n.m_value;
        }

        void root(numeral const & a, unsigned k, numeral & b) {
            if (k == 0)
                throw exception("0-th root is indeterminate");
            
            if (k == 1 || is_zero(a)) {
                set(b, a);
                return;
            }

            if (sign(a) < 0 && k % 2 == 0)
                throw exception("even root of negative number");
            
            // create the polynomial p of the form x^k - a
            value_ref_buffer p(*this);
            p.push_back(neg(a.m_value));
            for (unsigned i = 0; i < k - 1; i++)
                p.push_back(0);
            p.push_back(one());
            
            // TODO: invoke isolate_roots
        }
      
        void power(numeral const & a, unsigned k, numeral & b) {
            unsigned mask = 1;
            value_ref power(*this);
            power = a.m_value;
            set(b, one());
            while (mask <= k) {
                checkpoint();
                if (mask & k)
                    set(b, mul(b.m_value, power));
                power = mul(power, power);
                mask = mask << 1;
            }
        }

        /**
           \brief Remove 0s
        */
        void adjust_size(value_ref_buffer & r) {
            while (!r.empty() && r.back() == 0) {
                r.pop_back();
            }
        }

        /**
           \brief r <- p1 + p2
        */
        void add(unsigned sz1, value * const * p1, unsigned sz2, value * const * p2, value_ref_buffer & r) {
            r.reset();
            unsigned min = std::min(sz1, sz2);
            unsigned i = 0;
            for (; i < min; i++)
                r.push_back(add(p1[i], p2[i]));
            for (; i < sz1; i++)
                r.push_back(p1[i]);
            for (; i < sz2; i++)
                r.push_back(p2[i]);
            SASSERT(r.size() == std::max(sz1, sz2));
            adjust_size(r);
        }

        /**
           \brief r <- p + a
        */
        void add(unsigned sz, value * const * p, value * a, value_ref_buffer & r) {
            SASSERT(sz > 0);
            r.reset();
            r.push_back(add(p[0], a));
            r.append(sz - 1, p + 1);
            adjust_size(r);
        }

        /**
           \brief r <- p1 - p2
        */
        void sub(unsigned sz1, value * const * p1, unsigned sz2, value * const * p2, value_ref_buffer & r) {
            r.reset();
            unsigned min = std::min(sz1, sz2);
            unsigned i = 0;
            for (; i < min; i++)
                r.push_back(sub(p1[i], p2[i]));
            for (; i < sz1; i++)
                r.push_back(p1[i]);
            for (; i < sz2; i++)
                r.push_back(neg(p2[i]));
            SASSERT(r.size() == std::max(sz1, sz2));
            adjust_size(r);
        }

        /**
           \brief r <- p - a
        */
        void sub(unsigned sz, value * const * p, value * a, value_ref_buffer & r) {
            SASSERT(sz > 0);
            r.reset();
            r.push_back(sub(p[0], a));
            r.append(sz - 1, p + 1);
            adjust_size(r);
        }

        /**
           \brief r <- a * p
        */
        void mul(value * a, unsigned sz, value * const * p, value_ref_buffer & r) {
            r.reset();
            if (a == 0) 
                return;
            for (unsigned i = 0; i < sz; i++)
                r.push_back(mul(a, p[i]));
        }

        /**
           \brief r <- p1 * p2
        */
        void mul(unsigned sz1, value * const * p1, unsigned sz2, value * const * p2, value_ref_buffer & r) {
            r.reset();
            unsigned sz = sz1*sz2;
            r.resize(sz);
            if (sz1 < sz2) {
                std::swap(sz1, sz2);
                std::swap(p1, p2);
            }
            value_ref tmp(*this);
            for (unsigned i = 0; i < sz1; i++) {
                checkpoint();
                if (p1[i] == 0)
                    continue;
                for (unsigned j = 0; j < sz2; j++) {
                    // r[i+j] <- r[i+j] + p1[i]*p2[j]
                    tmp = mul(p1[i], p2[j]);
                    r.set(i+j, add(r[i+j], tmp));
                }
            }
            adjust_size(r);
        }

        /**
           \brief p <- p/a
        */
        void div(value_ref_buffer & p, value * a) {
            SASSERT(!is_zero(a));
            if (is_rational_one(a))
                return;
            unsigned sz = p.size();
            for (unsigned i = 0; i < sz; i++)
                p.set(i, div(p[i], a));
        }

        /**
           \brief q <- quotient(p1, p2); r <- rem(p1, p2)
        */
        void div_rem(unsigned sz1, value * const * p1, unsigned sz2, value * const * p2, 
                     value_ref_buffer & q, value_ref_buffer & r) {
            SASSERT(sz2 > 0);
            if (sz2 == 1) {
                q.reset(); q.append(sz1, p1);
                div(q, *p2);
                r.reset();
            }
            else {
                q.reset();
                r.reset(); r.append(sz1, p1);
                if (sz1 > 1) {
                    if (sz1 >= sz2) {
                        q.resize(sz1 - sz2 + 1);
                    }
                    else {
                        SASSERT(q.empty());
                    }
                    value * b_n = p2[sz2-1];
                    SASSERT(!is_zero(b_n));
                    value_ref ratio(*this);
                    while (true) {
                        checkpoint();
                        sz1 = r.size();
                        if (sz1 < sz2) {
                            adjust_size(q);
                            break;
                        }
                        unsigned m_n = sz1 - sz2; // m-n            
                        ratio = div(r[sz1 - 1], b_n);
                        // q[m_n] <- q[m_n] + r[sz1 - 1]/b_n
                        q.set(m_n, add(q[m_n], ratio));
                        for (unsigned i = 0; i < sz2 - 1; i++) {
                            // r[i + m_n] <- r[i + m_n] - ratio * p2[i]
                            ratio = mul(ratio, p2[i]);
                            r.set(i + m_n, sub(r[i + m_n], ratio));
                        }
                        r.shrink(sz1 - 1);
                        adjust_size(r);
                    }
                }
            }
        }
        
        /**
           \brief q <- quotient(p1, p2)
        */
        void div(unsigned sz1, value * const * p1, unsigned sz2, value * const * p2, value_ref_buffer & q) {
            value_ref_buffer r(*this);
            div_rem(sz1, p1, sz2, p2, q, r);
        }
        
        /**
           \brief r <- p/a
        */
        void div(unsigned sz, value * const * p, value * a, value_ref_buffer & r) {
            for (unsigned i = 0; i < sz; i++) {
                r.push_back(div(p[i], a));
            }
        }

        /**
           \brief r <- rem(p1, p2)
        */
        void rem(unsigned sz1, value * const * p1, unsigned sz2, value * const * p2, value_ref_buffer & r) {
            r.reset();
            SASSERT(sz2 > 0);
            if (sz2 == 1)
                return;
            r.append(sz1, p1);
            if (sz1 <= 1)
                return; // r is p1
            value * b_n = p2[sz2 - 1];
            value_ref ratio(*this);
            SASSERT(b_n != 0);
            while (true) {
                checkpoint();
                sz1 = r.size();
                if (sz1 < sz2) {
                    return;
                }
                unsigned m_n = sz1 - sz2;
                ratio = div(r[sz1 - 1], b_n);
                for (unsigned i = 0; i < sz2 - 1; i++) {
                    ratio = mul(ratio, p2[i]);
                    r.set(i + m_n, sub(r[i + m_n], ratio));
                }
                r.shrink(sz1 - 1);
                adjust_size(r);
            }
        }

        /**
           \brief r <- -p
        */
        void neg(unsigned sz, value * const * p, value_ref_buffer & r) {
            r.reset();
            for (unsigned i = 0; i < sz; i++) 
                r.push_back(neg(p[i]));
        }
        
        /**
           \brief r <- -r
        */
        void neg(value_ref_buffer & r) {
            unsigned sz = r.size();
            for (unsigned i = 0; i < sz; i++) 
                r.set(i, neg(r[i]));
        }

        /**
           \brief p <- -p
        */
        void neg(polynomial & p) {
            unsigned sz = p.size();
            for (unsigned i = 0; i < sz; i++) {
                value * v = neg(p[i]);
                inc_ref(v);
                dec_ref(p[i]);
                p[i] = v;
            }
        }

        /**
           \brief r <- srem(p1, p2)  
           Signed remainder
        */
        void srem(unsigned sz1, value * const * p1, unsigned sz2, value * const * p2, value_ref_buffer & r) {
            rem(sz1, p1, sz2, p2, r);
            neg(r);
        }
        
        /**
           \brief Force the leading coefficient of p to be 1.
        */
        void mk_monic(value_ref_buffer & p) {
            unsigned sz = p.size();
            if (sz > 0) {
                SASSERT(p[sz-1] != 0);
                if (!is_rational_one(p[sz-1])) {
                    for (unsigned i = 0; i < sz - 1; i++) {
                        p.set(i, div(p[i], p[sz-1]));
                    }
                    p.set(sz-1, one());
                }
            }
        }

        /**
           \brief r <- gcd(p1, p2)
        */
        void gcd(unsigned sz1, value * const * p1, unsigned sz2, value * const * p2, value_ref_buffer & r) {
            if (sz1 == 0) {
                r.append(sz2, p2);
                mk_monic(r);
            }
            else if (sz2 == 0) {
                r.append(sz1, p1);
                mk_monic(r);
            }
            else {
                value_ref_buffer A(*this);
                value_ref_buffer B(*this);
                value_ref_buffer & R = r;
                A.append(sz1, p1);
                B.append(sz2, p2);
                while (true) {
                    if (B.empty()) {
                        mk_monic(A);
                        r = A;
                        return;
                    }
                    rem(A.size(), A.c_ptr(), B.size(), B.c_ptr(), R);
                    A = B;
                    B = R;
                }
            }
        }

        /**
           \brief r <- dp/dx
        */
        void derivative(unsigned sz, value * const * p, value_ref_buffer & r) {
            r.reset();
            if (sz > 1) {
                for (unsigned i = 1; i < sz; i++) {
                    mpq i_mpq(i);
                    value_ref i_value(*this);
                    i_value = mk_rational(i_mpq);
                    r.push_back(mul(i_value, p[i]));
                }
                adjust_size(r);
            }
        }

        /**
           \brief r <- squarefree(p)
           Store in r the square free factors of p.
        */
        void square_free(unsigned sz, value * const * p, value_ref_buffer & r) {
            if (sz <= 1) {
                r.append(sz, p);
            }
            else {
                value_ref_buffer p_prime(*this);
                value_ref_buffer g(*this);
                derivative(sz, p, p_prime);
                gcd(sz, p, p_prime.size(), p_prime.c_ptr(), g);
                if (g.size() <= 1) {
                    r.append(sz, p);
                }
                else {
                    div(sz, p, g.size(), g.c_ptr(), r);
                }
            }
        }

        /**
           \brief Keep expanding the Sturm sequence starting at seq
        */
        void sturm_seq_core(scoped_polynomial_seq & seq) {
            value_ref_buffer r(*this);
            while (true) {
                unsigned sz = seq.size();
                srem(seq.size(sz-2), seq.coeffs(sz-2), seq.size(sz-1), seq.coeffs(sz-1), r);
                if (r.empty())
                    return;
                seq.push(r.size(), r.c_ptr());
            }
        }
        
        /**
           \brief Store in seq the Sturm sequence for (p1; p2)
        */
        void sturm_seq(unsigned sz1, value * const * p1, unsigned sz2, value * const * p2, scoped_polynomial_seq & seq) {
            seq.reset();
            seq.push(sz1, p1);
            seq.push(sz2, p2);
            sturm_seq_core(seq);
        }
        
        /**
           \brief Store in seq the Sturm sequence for (p; p')
        */
        void sturm_seq(unsigned sz, value * const * p, scoped_polynomial_seq & seq) {
            seq.reset();
            value_ref_buffer p_prime(*this);
            seq.push(sz, p);
            derivative(sz, p, p_prime);
            seq.push(p_prime.size(), p_prime.c_ptr());
            sturm_seq_core(seq);
        }

        /**
           \brief Store in seq the Sturm sequence for (p1; p1' * p2)
        */
        void sturm_tarski_seq(unsigned sz1, value * const * p1, unsigned sz2, value * const * p2, scoped_polynomial_seq & seq) {
            seq.reset();
            value_ref_buffer p1p2(*this);
            seq.push(sz1, p1);
            derivative(sz1, p1, p1p2);
            mul(p1p2.size(), p1p2.c_ptr(), sz2, p2, p1p2);
            seq.push(p1p2.size(), p1p2.c_ptr());
            sturm_seq_core(seq);
        }

        void refine_rational_interval(rational_value * v, unsigned prec) {
            mpbqi & i = interval(v);
            if (!i.lower_is_open() && !i.lower_is_open()) {
                SASSERT(bqm().eq(i.lower(), i.upper()));
                return;
            }
            while (!check_precision(i, prec)) {
                checkpoint();
                bqm().refine_lower(to_mpq(v), i.lower(), i.upper());
                bqm().refine_upper(to_mpq(v), i.lower(), i.upper());
            }
        }

        /**
           \brief Refine the interval for each coefficient of in the polynomial p.
        */
        bool refine_coeffs_interval(polynomial const & p, unsigned prec) {
            unsigned sz = p.size();
            for (unsigned i = 0; i < sz; i++) {
                if (p[i] != 0 && !refine_interval(p[i], prec))
                    return false;
            }
            return true;
        }

        /**
           \brief Store in r the interval p(v).
        */
        void polynomial_interval(polynomial const & p, mpbqi const & v, mpbqi & r) {
            // We compute r using the Horner Sequence
            //  ((a_n * v + a_{n-1})*v + a_{n-2})*v + a_{n-3} ...
            // where a_i's are the coefficients of p.
            unsigned sz = p.size();
            if (sz == 1) {
                bqim().set(r, interval(p[0]));
            }
            else {
                SASSERT(sz > 0);
                SASSERT(p[sz - 1] != 0);
                // r <- a_n * v
                bqim().mul(interval(p[sz-1]), v, r); 
                unsigned i = sz - 1;
                while (i > 0) {
                    --i;
                    if (p[i] != 0)
                        bqim().add(r, interval(p[i]), r);
                    if (i > 0)
                        bqim().mul(r, v, r);
                }
            }
        }

        /**
           \brief Update the interval of v by using the intervals of 
           extension and coefficients of the rational function.
        */
        void update_rf_interval(rational_function_value * v, unsigned prec) {
            if (is_rational_one(v->den())) {
                polynomial_interval(v->num(), v->ext()->interval(), v->interval());
            }
            else  {
                scoped_mpbqi num_i(bqim()), den_i(bqim());
                polynomial_interval(v->num(), v->ext()->interval(), num_i);
                polynomial_interval(v->den(), v->ext()->interval(), den_i);
                div(num_i, den_i, inc_precision(prec, 2), v->interval());
            }
        }

        void refine_transcendental_interval(rational_function_value * v, unsigned prec) {
            SASSERT(v->ext()->is_transcendental());
            polynomial const & n = v->num();
            polynomial const & d = v->den();
            unsigned _prec = prec;
            while (true) {
                VERIFY(refine_coeffs_interval(n, _prec)); // must return true because a transcendental never depends on an infinitesimal
                VERIFY(refine_coeffs_interval(d, _prec)); // must return true because a transcendental never depends on an infinitesimal
                refine_transcendental_interval(to_transcendental(v->ext()), _prec);
                update_rf_interval(v, prec);
                
                TRACE("rcf", tout << "after update_rf_interval: " << magnitude(v->interval()) << " ";
                      bqim().display(tout, v->interval()); tout << std::endl;);
                
                if (check_precision(v->interval(), prec))
                    return;
                _prec++;
            }
        }

        bool refine_infinitesimal_interval(rational_function_value * v, unsigned prec) {
            SASSERT(v->ext()->is_infinitesimal());
            polynomial const & numerator   = v->num();
            polynomial const & denominator = v->den();
            unsigned num_idx = first_non_zero(numerator);
            unsigned den_idx = first_non_zero(denominator);
            if (num_idx == 0 && den_idx == 0) {
                unsigned _prec = prec;
                while (true) {
                    refine_interval(numerator[num_idx],   _prec);
                    refine_interval(denominator[num_idx], _prec);
                    mpbqi const & num_i = interval(numerator[num_idx]);
                    mpbqi const & den_i = interval(denominator[num_idx]);
                    SASSERT(!contains_zero(num_i));
                    SASSERT(!contains_zero(den_i));
                    if (is_open_interval(num_i) && is_open_interval(den_i)) {
                        // This case is simple because adding/subtracting infinitesimal quantities, will
                        // not change the interval.
                        div(num_i, den_i, inc_precision(prec, 2), v->interval());
                    }
                    else {
                        // The intervals num_i and den_i may not be open.
                        // Example: numerator[num_idx] or denominator[num_idx] are rationals
                        // that can be precisely represented as binary rationals.
                        scoped_mpbqi new_num_i(bqim());
                        scoped_mpbqi new_den_i(bqim());
                        mpbq tiny_value(1, _prec*2);
                        if (numerator.size() > 1)
                            add_infinitesimal(num_i, sign_of_first_non_zero(numerator, 1) > 0,   tiny_value, new_num_i);
                        else
                            bqim().set(new_num_i, num_i);
                        if (denominator.size() > 1)
                            add_infinitesimal(den_i, sign_of_first_non_zero(denominator, 1) > 0, tiny_value, new_den_i);
                        else
                            bqim().set(new_den_i, den_i);
                        div(new_num_i, new_den_i, inc_precision(prec, 2), v->interval());
                    }
                    if (check_precision(v->interval(), prec))
                        return true;
                    _prec++;
                }
            }
            else { 
                // The following condition must hold because gcd(numerator, denominator) == 1
                // If num_idx > 0 and den_idx > 0, eps^{min(num_idx, den_idx)} is a factor of gcd(numerator, denominator) 
                SASSERT(num_idx ==  0 || den_idx == 0);
                int s = sign(numerator[num_idx]) * sign(denominator[den_idx]);
                // The following must hold since numerator[num_idx] and denominator[den_idx] are not zero.
                SASSERT(s != 0); 
                if (num_idx == 0) {
                    SASSERT(den_idx > 0);
                    // |v| is bigger than any binary rational
                    // Interval can't be refined. There is no way to isolate an infinity with an interval with binary rational end points.
                    return false;
                }
                else {
                    SASSERT(num_idx > 0);
                    SASSERT(den_idx == 0);
                    // |v| is infinitely close to zero.
                    if (s == 1) {
                        // it is close from above
                        set_lower(v->interval(), mpbq(0));
                        set_upper(v->interval(), mpbq(1, prec));
                    }
                    else {
                        // it is close from below
                        set_lower(v->interval(), mpbq(-1, prec));
                        set_upper(v->interval(), mpbq(0));
                    }
                    return true;
                }
            }
        }

        bool refine_algebraic_interval(rational_function_value * v, unsigned prec) {
            // TODO
            return false;
        }

        /**
           \brief Refine the interval of v to the desired precision (1/2^k).
           Return false in case of failure. A failure can only happen if v depends on infinitesimal values.
        */
        bool refine_interval(value * v, unsigned prec) {
            checkpoint();
            SASSERT(!is_zero(v));
            int m = magnitude(interval(v));
            if (m == INT_MIN || (m < 0 && static_cast<unsigned>(-m) > prec))
                return true;
            save_interval_if_too_small(v);
            if (is_nz_rational(v)) {
                refine_rational_interval(to_nz_rational(v), prec);
                return true;
            }
            else { 
                rational_function_value * rf = to_rational_function(v);
                if (rf->ext()->is_transcendental()) {
                    refine_transcendental_interval(rf, prec);
                    return true;
                }
                else if (rf->ext()->is_infinitesimal())
                    return refine_infinitesimal_interval(rf, prec);
                else
                    return refine_algebraic_interval(rf, prec);
            }
        }

        /**
           \brief Return the position of the first non-zero coefficient of p.
         */
        static unsigned first_non_zero(polynomial const & p) {
            unsigned sz = p.size();
            for (unsigned i = 0; i < sz; i++) {
                if (p[i] != 0)
                    return i;
            }
            UNREACHABLE();
            return UINT_MAX;
        }

        /**
           \brief Return the sign of the first non zero coefficient starting at position start_idx
        */
        int sign_of_first_non_zero(polynomial const & p, unsigned start_idx) {
            unsigned sz = p.size();
            SASSERT(start_idx < sz);
            for (unsigned i = start_idx; i < sz; i++) {
                if (p[i] != 0)
                    return sign(p[i]);
            }
            UNREACHABLE();
            return 0;
        }
        
        /**
           out <- in + infinitesimal (if plus_eps == true) 
           out <- in - infinitesimal (if plus_eps == false)

           We use the following rules for performing the assignment
           
           If plus_eps == True
               If lower(in) == v (closed or open), then lower(out) == v and open
               If upper(in) == v and open,         then upper(out) == v and open
               If upper(in) == v and closed,       then upper(out) == new_v and open
                        where new_v is v + tiny_value / 2^k, where k is the smallest natural such that sign(new_v) == sign(v)
           If plus_eps == False
               If lower(in) == v and open,         then lower(out) == v and open
               If lower(in) == v and closed,       then lower(out) == new_v and open 
               If upper(in) == v (closed or open), then upper(out) == v and open
                        where new_v is v - tiny_value / 2^k, where k is the smallest natural such that sign(new_v) == sign(v)
        */
        void add_infinitesimal(mpbqi const & in, bool plus_eps, mpbq const & tiny_value, mpbqi & out) {
            set_interval(out, in);
            out.set_lower_is_open(true);
            out.set_upper_is_open(true);
            if (plus_eps) {
                if (!in.upper_is_open()) {
                    scoped_mpbq tval(bqm());
                    tval = tiny_value;
                    while (true) {
                        bqm().add(in.upper(), tval, out.upper());
                        if (bqm().is_pos(in.upper()) == bqm().is_pos(out.upper()))
                            return;
                        bqm().div2(tval);
                        checkpoint();
                    }
                }
            }
            else {
                if (!in.lower_is_open()) {
                    scoped_mpbq tval(bqm());
                    tval = tiny_value;
                    while (true) {
                        bqm().sub(in.lower(), tval, out.lower());
                        if (bqm().is_pos(in.lower()) == bqm().is_pos(out.lower()))
                            return;
                        bqm().div2(tval);
                        checkpoint();
                    }
                }
            }
        }

        /**
           \brief Determine the sign of an element of Q(trans_0, ..., trans_n)
        */
        void determine_transcendental_sign(rational_function_value * v) {
            // Remark: the sign of a rational function value on an transcendental is never zero.
            // Reason: The transcendental can be the root of a polynomial.
            SASSERT(v->ext()->is_transcendental());
            int m = magnitude(v->interval());
            unsigned prec = 1;
            if (m < 0)
                prec = static_cast<unsigned>(-m) + 1;
            while (contains_zero(v->interval())) {
                refine_transcendental_interval(v, prec);
                prec++;
            }
        }

        /**
           \brief Determine the sign of an element of Q(trans_0, ..., trans_n, eps_0, ..., eps_m)
        */
        void determine_infinitesimal_sign(rational_function_value * v) {
            // Remark: the sign of a rational function value on an infinitesimal is never zero.
            // Reason: An infinitesimal eps is transcendental in any field K. So, it can't be the root
            // of a polynomial.
            SASSERT(v->ext()->is_infinitesimal());
            polynomial const & numerator   = v->num();
            polynomial const & denominator = v->den();
            unsigned num_idx = first_non_zero(numerator);
            unsigned den_idx = first_non_zero(denominator);
            if (num_idx == 0 && den_idx == 0) {
                mpbqi const & num_i = interval(numerator[num_idx]);
                mpbqi const & den_i = interval(denominator[num_idx]);
                SASSERT(!contains_zero(num_i));
                SASSERT(!contains_zero(den_i));
                if (is_open_interval(num_i) && is_open_interval(den_i)) {
                    // This case is simple because adding/subtracting infinitesimal quantities, will
                    // not change the interval.
                    div(num_i, den_i, m_ini_precision, v->interval());
                }
                else {
                    // The intervals num_i and den_i may not be open.
                    // Example: numerator[num_idx] or denominator[num_idx] are rationals
                    // that can be precisely represented as binary rationals.
                    scoped_mpbqi new_num_i(bqim());
                    scoped_mpbqi new_den_i(bqim());
                    mpbq tiny_value(1, m_ini_precision); // 1/2^{m_ini_precision}
                    if (numerator.size() > 1)
                        add_infinitesimal(num_i, sign_of_first_non_zero(numerator, 1) > 0,   tiny_value, new_num_i);
                    else
                        bqim().set(new_num_i, num_i);
                    if (denominator.size() > 1)
                        add_infinitesimal(den_i, sign_of_first_non_zero(denominator, 1) > 0, tiny_value, new_den_i);
                    else
                        bqim().set(new_den_i, den_i);
                    div(new_num_i, new_den_i, m_ini_precision, v->interval());
                }
            }
            else { 
                // The following condition must hold because gcd(numerator, denominator) == 1
                // If num_idx > 0 and den_idx > 0, eps^{min(num_idx, den_idx)} is a factor of gcd(numerator, denominator) 
                SASSERT(num_idx ==  0 || den_idx == 0);
                int s = sign(numerator[num_idx]) * sign(denominator[den_idx]);
                // The following must hold since numerator[num_idx] and denominator[den_idx] are not zero.
                SASSERT(s != 0); 
                if (num_idx == 0) {
                    SASSERT(den_idx > 0);
                    // |v| is bigger than any binary rational
                    if (s == 1) {
                        // it is "oo"
                        set_lower(v->interval(), m_plus_inf_approx);
                        set_upper_inf(v->interval());
                    }
                    else {
                        // it is "-oo"
                        set_lower_inf(v->interval());
                        set_upper(v->interval(), m_minus_inf_approx);
                    }
                }
                else {
                    SASSERT(num_idx > 0);
                    SASSERT(den_idx == 0);
                    // |v| is infinitely close to zero.
                    if (s == 1) {
                        // it is close from above
                        set_lower(v->interval(), mpbq(0));
                        set_upper(v->interval(), mpbq(1, m_ini_precision));
                    }
                    else {
                        // it is close from below
                        set_lower(v->interval(), mpbq(-1, m_ini_precision));
                        set_upper(v->interval(), mpbq(0));
                    }
                }
            }
            SASSERT(!contains_zero(v->interval()));
        }

        bool determine_algebraic_sign(rational_function_value * v) {
            // TODO
            return false;
        }

        /**
           \brief Determine the sign of the new rational function value.
           The idea is to keep refinining the interval until interval of v does not contain 0.
           After a couple of steps we switch to expensive sign determination procedure.

           Return false if v is actually zero.
        */
        bool determine_sign(rational_function_value * v) {
            if (!contains_zero(v->interval()))
                return true;
            switch (v->ext()->knd()) {
            case extension::TRANSCENDENTAL: determine_transcendental_sign(v); return true; // it is never zero
            case extension::INFINITESIMAL:  determine_infinitesimal_sign(v); return true; // it is never zero
            case extension::ALGEBRAIC:      return determine_algebraic_sign(v);
            default:
                UNREACHABLE();
                return false;
            }
        }

        /**
           \brief Set new_p1 and new_p2 using the following normalization rules:
           - new_p1 <- p1/p2[0];       new_p2 <- one              IF  sz2 == 1
           - new_p1 <- one;            new_p2 <- p2/p1[0];        IF  sz1 == 1
           - new_p1 <- p1/gcd(p1, p2); new_p2 <- p2/gcd(p1, p2);  Otherwise
        */
        void normalize(unsigned sz1, value * const * p1, unsigned sz2, value * const * p2, value_ref_buffer & new_p1, value_ref_buffer & new_p2) {
            SASSERT(sz1 > 0 && sz2 > 0);
            if (sz2 == 1) {
                // - new_p1 <- p1/p2[0];       new_p2 <- one              IF  sz2 == 1
                div(sz1, p1, p2[0], new_p1);
                new_p2.reset(); new_p2.push_back(one());
            }
            else if (sz1 == 1) {
                SASSERT(sz2 > 1);
                // - new_p1 <- one;            new_p2 <- p2/p1[0];        IF  sz1 == 1
                new_p1.reset(); new_p1.push_back(one());
                div(sz2, p2, p1[0], new_p2);
            }
            else {
                // - new_p1 <- p1/gcd(p1, p2); new_p2 <- p2/gcd(p1, p2);  Otherwise
                value_ref_buffer g(*this);
                gcd(sz1, p1, sz2, p2, g);
                if (is_rational_one(g)) {
                    new_p1.append(sz1, p1);
                    new_p2.append(sz2, p2);
                }
                else if (g.size() == sz1 || g.size() == sz2) {
                    // After dividing p1 and p2 by g, one of the quotients will have size 1.
                    // Thus, we have to apply the first two rules again.
                    value_ref_buffer tmp_p1(*this);
                    value_ref_buffer tmp_p2(*this);
                    div(sz1, p1, g.size(), g.c_ptr(), tmp_p1);
                    div(sz2, p2, g.size(), g.c_ptr(), tmp_p2);
                    if (tmp_p2.size() == 1) {
                        div(tmp_p1.size(), tmp_p1.c_ptr(), tmp_p2[0], new_p1);
                        new_p2.reset(); new_p2.push_back(one());
                    }
                    else if (tmp_p1.size() == 1) {
                        SASSERT(tmp_p2.size() > 1);
                        new_p1.reset(); new_p1.push_back(one());
                        div(tmp_p2.size(), tmp_p2.c_ptr(), tmp_p1[0], new_p2);
                    }
                    else {
                        UNREACHABLE();
                    }
                }
                else {
                    div(sz1, p1, g.size(), g.c_ptr(), new_p1);
                    div(sz2, p2, g.size(), g.c_ptr(), new_p2);
                    SASSERT(new_p1.size() > 1);
                    SASSERT(new_p2.size() > 1);
                }
            }
        }

        /**
           \brief Create a new value using the a->ext(), and the given numerator and denominator.
           Use interval(a) + interval(b) as an initial approximation for the interval of the result, and invoke determine_sign()
        */
        value * mk_add_value(rational_function_value * a, value * b, unsigned num_sz, value * const * num, unsigned den_sz, value * const * den) {
            SASSERT(num_sz > 0 && den_sz > 0);
            if (num_sz == 1 && den_sz == 1) {
                // In this case, the normalization rules guarantee that den is one.
                SASSERT(is_rational_one(den[0]));
                return num[0];
            }
            rational_function_value * r = mk_rational_function_value_core(a->ext(), num_sz, num, den_sz, den);
            bqim().add(interval(a), interval(b), r->interval());
            if (determine_sign(r)) {
                return r;
            }
            else {
                // The new value is 0
                del_rational_function(r);
                return 0;
            }
        }

        /**
           \brief Add a value of 'a' the form n/1 with b where rank(a) > rank(b)
        */
        value * add_p_v(rational_function_value * a, value * b) {
            SASSERT(is_rational_one(a->den()));
            SASSERT(compare_rank(a, b) > 0);
            polynomial const & an  = a->num();
            polynomial const & one = a->den();
            SASSERT(an.size() > 1);
            value_ref_buffer new_num(*this);
            add(an.size(), an.c_ptr(), b, new_num);
            SASSERT(new_num.size() == an.size());
            return mk_add_value(a, b, new_num.size(), new_num.c_ptr(), one.size(), one.c_ptr());
        }
        
        /**
           \brief Add a value 'a' of the form n/d with b where rank(a) > rank(b)
        */
        value * add_rf_v(rational_function_value * a, value * b) {
            value_ref_buffer b_ad(*this);
            value_ref_buffer num(*this);
            polynomial const & an = a->num();
            polynomial const & ad = a->den();
            if (is_rational_one(ad))
                return add_p_v(a, b);
            // b_ad <- b * ad
            mul(b, ad.size(), ad.c_ptr(), b_ad);
            // num <- a + b * ad
            add(an.size(), an.c_ptr(), b_ad.size(), b_ad.c_ptr(), num);
            if (num.empty())
                return 0;
            value_ref_buffer new_num(*this);
            value_ref_buffer new_den(*this);
            normalize(num.size(), num.c_ptr(), ad.size(), ad.c_ptr(), new_num, new_den);
            return mk_add_value(a, b, new_num.size(), new_num.c_ptr(), new_den.size(), new_den.c_ptr());
        }

        /**
           \brief Add values 'a' and 'b' of the form n/1 and rank(a) == rank(b)
        */
        value * add_p_p(rational_function_value * a, rational_function_value * b) {
            SASSERT(is_rational_one(a->den()));
            SASSERT(is_rational_one(b->den()));
            SASSERT(compare_rank(a, b) == 0);
            polynomial const & an  = a->num();
            polynomial const & one = a->den();
            polynomial const & bn  = b->num();
            value_ref_buffer new_num(*this);
            add(an.size(), an.c_ptr(), bn.size(), bn.c_ptr(), new_num);
            if (new_num.empty())
                return 0;
            return mk_add_value(a, b, new_num.size(), new_num.c_ptr(), one.size(), one.c_ptr());
        }
        
        /**
           \brief Add values 'a' and 'b' of the form n/d and rank(a) == rank(b)
        */
        value * add_rf_rf(rational_function_value * a, rational_function_value * b) {
            SASSERT(compare_rank(a, b) == 0);
            polynomial const & an = a->num();
            polynomial const & ad = a->den();
            polynomial const & bn = b->num();
            polynomial const & bd = b->den();
            if (is_rational_one(ad) && is_rational_one(bd))
                return add_p_p(a, b);
            value_ref_buffer an_bd(*this);
            value_ref_buffer bn_ad(*this);
            mul(an.size(), an.c_ptr(), bd.size(), bd.c_ptr(), an_bd);
            mul(bn.size(), bn.c_ptr(), ad.size(), ad.c_ptr(), bn_ad);
            value_ref_buffer num(*this);
            add(an_bd.size(), an_bd.c_ptr(), bn_ad.size(), bn_ad.c_ptr(), num);
            if (num.empty())
                return 0;
            value_ref_buffer den(*this);
            mul(ad.size(), ad.c_ptr(), bd.size(), bd.c_ptr(), den);
            value_ref_buffer new_num(*this);
            value_ref_buffer new_den(*this);
            normalize(num.size(), num.c_ptr(), den.size(), den.c_ptr(), new_num, new_den);
            return mk_add_value(a, b, new_num.size(), new_num.c_ptr(), new_den.size(), new_den.c_ptr());
        }
        
        value * add(value * a, value * b) {
            if (a == 0)
                return b;
            else if (b == 0)
                return a;
            else if (is_nz_rational(a) && is_nz_rational(b)) {
                scoped_mpq r(qm());
                qm().add(to_mpq(a), to_mpq(b), r);
                if (qm().is_zero(r))
                    return 0;
                else 
                    return mk_rational(r);
            }
            else {
                switch (compare_rank(a, b)) {
                case -1: return add_rf_v(to_rational_function(b), a); 
                case 0:  return add_rf_rf(to_rational_function(a), to_rational_function(b));
                case 1:  return add_rf_v(to_rational_function(a), b);
                default: UNREACHABLE();
                    return 0;
                }
            }
        }
        
        value * sub(value * a, value * b) {
            if (a == 0)
                return neg(b);
            else if (b == 0)
                return a;
            else if (is_nz_rational(a) && is_nz_rational(b)) {
                scoped_mpq r(qm());
                qm().sub(to_mpq(a), to_mpq(b), r);
                if (qm().is_zero(r))
                    return 0;
                else 
                    return mk_rational(r);
            }
            else {
                value_ref neg_b(*this);
                neg_b = neg(b);
                switch (compare_rank(a, neg_b)) {
                case -1: return add_rf_v(to_rational_function(neg_b), a); 
                case 0:  return add_rf_rf(to_rational_function(a), to_rational_function(neg_b));
                case 1:  return add_rf_v(to_rational_function(a), b);
                default: UNREACHABLE();
                    return 0;
                }
            }
        }

        value * neg_rf(rational_function_value * a) {
            polynomial const & an = a->num();
            polynomial const & ad = a->den();
            value_ref_buffer new_num(*this);
            neg(an.size(), an.c_ptr(), new_num);
            rational_function_value * r = mk_rational_function_value_core(a->ext(), new_num.size(), new_num.c_ptr(), ad.size(), ad.c_ptr());
            bqim().neg(interval(a), r->interval());
            return r;
        }

        value * neg(value * a) {
            if (a == 0)
                return 0;
            else if (is_nz_rational(a)) {
                scoped_mpq r(qm());
                qm().set(r, to_mpq(a));
                qm().neg(r);
                return mk_rational(r);
            }
            else {
                return neg_rf(to_rational_function(a));
            }
        }

        /**
           \brief Create a new value using the a->ext(), and the given numerator and denominator.
           Use interval(a) * interval(b) as an initial approximation for the interval of the result, and invoke determine_sign()
        */
        value * mk_mul_value(rational_function_value * a, value * b, unsigned num_sz, value * const * num, unsigned den_sz, value * const * den) {
            SASSERT(num_sz > 0 && den_sz > 0);
            if (num_sz == 1 && den_sz == 1) {
                // In this case, the normalization rules guarantee that den is one.
                SASSERT(is_rational_one(den[0]));
                return num[0];
            }
            rational_function_value * r = mk_rational_function_value_core(a->ext(), num_sz, num, den_sz, den);
            bqim().mul(interval(a), interval(b), r->interval());
            if (determine_sign(r)) {
                return r;
            }
            else {
                // The new value is 0
                del_rational_function(r);
                return 0;
            }
        }

        /**
           \brief Multiply a value of 'a' the form n/1 with b where rank(a) > rank(b)
        */
        value * mul_p_v(rational_function_value * a, value * b) {
            SASSERT(is_rational_one(a->den()));
            SASSERT(b != 0);
            SASSERT(compare_rank(a, b) > 0);
            polynomial const & an  = a->num();
            polynomial const & one = a->den();
            SASSERT(an.size() > 1);
            value_ref_buffer new_num(*this);
            mul(b, an.size(), an.c_ptr(), new_num);
            SASSERT(new_num.size() == an.size());
            return mk_mul_value(a, b, new_num.size(), new_num.c_ptr(), one.size(), one.c_ptr());
        }

        /**
           \brief Multiply a value 'a' of the form n/d with b where rank(a) > rank(b)
        */
        value * mul_rf_v(rational_function_value * a, value * b) {
            polynomial const & an = a->num();
            polynomial const & ad = a->den();
            if (is_rational_one(ad))
                return mul_p_v(a, b);
            value_ref_buffer num(*this);
            // num <- b * an
            mul(b, an.size(), an.c_ptr(), num);
            SASSERT(num.size() == an.size());
            value_ref_buffer new_num(*this);
            value_ref_buffer new_den(*this);
            normalize(num.size(), num.c_ptr(), ad.size(), ad.c_ptr(), new_num, new_den);
            return mk_mul_value(a, b, new_num.size(), new_num.c_ptr(), new_den.size(), new_den.c_ptr());
        }

        /**
           \brief Multiply values 'a' and 'b' of the form n/1 and rank(a) == rank(b)
        */
        value * mul_p_p(rational_function_value * a, rational_function_value * b) {
            SASSERT(is_rational_one(a->den()));
            SASSERT(is_rational_one(b->den()));
            SASSERT(compare_rank(a, b) == 0);
            polynomial const & an  = a->num();
            polynomial const & one = a->den();
            polynomial const & bn  = b->num();
            value_ref_buffer new_num(*this);
            mul(an.size(), an.c_ptr(), bn.size(), bn.c_ptr(), new_num);
            SASSERT(!new_num.empty());
            return mk_mul_value(a, b, new_num.size(), new_num.c_ptr(), one.size(), one.c_ptr());
        }

        /**
           \brief Multiply values 'a' and 'b' of the form n/d and rank(a) == rank(b)
        */
        value * mul_rf_rf(rational_function_value * a, rational_function_value * b) {
            SASSERT(compare_rank(a, b) == 0);
            polynomial const & an = a->num();
            polynomial const & ad = a->den();
            polynomial const & bn = b->num();
            polynomial const & bd = b->den();
            if (is_rational_one(ad) && is_rational_one(bd))
                return mul_p_p(a, b);
            value_ref_buffer num(*this);
            value_ref_buffer den(*this);
            mul(an.size(), an.c_ptr(), bn.size(), bn.c_ptr(), num);
            mul(ad.size(), ad.c_ptr(), bd.size(), bd.c_ptr(), den);
            SASSERT(!num.empty()); SASSERT(!den.empty());
            value_ref_buffer new_num(*this);
            value_ref_buffer new_den(*this);
            normalize(num.size(), num.c_ptr(), den.size(), den.c_ptr(), new_num, new_den);
            return mk_mul_value(a, b, new_num.size(), new_num.c_ptr(), new_den.size(), new_den.c_ptr());
        }

        value * mul(value * a, value * b) {
            if (a == 0 || b == 0)
                return 0;
            else if (is_rational_one(a))
                return b;
            else if (is_rational_one(b))
                return a;
            else if (is_rational_minus_one(a))
                return neg(b);
            else if (is_rational_minus_one(b))
                return neg(a);
            else if (is_nz_rational(a) && is_nz_rational(b)) {
                scoped_mpq r(qm());
                qm().mul(to_mpq(a), to_mpq(b), r);
                return mk_rational(r);
            }
            else {
                switch (compare_rank(a, b)) {
                case -1: return mul_rf_v(to_rational_function(b), a); 
                case 0:  return mul_rf_rf(to_rational_function(a), to_rational_function(b));
                case 1:  return mul_rf_v(to_rational_function(a), b);
                default: UNREACHABLE();
                    return 0;
                }
            }
        }

        value * div(value * a, value * b) {
            if (a == 0)
                return 0;
            else if (b == 0)
                throw exception("division by zero");
            else if (is_rational_one(b))
                return a;
            else if (is_rational_one(a))
                return inv(b);
            else if (is_rational_minus_one(b))
                return neg(a);
            else if (is_nz_rational(a) && is_nz_rational(b)) {
                scoped_mpq r(qm());
                qm().div(to_mpq(a), to_mpq(b), r);
                return mk_rational(r);
            }
            else {
                value_ref inv_b(*this);
                inv_b = inv(b);
                switch (compare_rank(a, inv_b)) {
                case -1: return mul_rf_v(to_rational_function(inv_b), a); 
                case 0:  return mul_rf_rf(to_rational_function(a), to_rational_function(inv_b));
                case 1:  return mul_rf_v(to_rational_function(a), inv_b);
                default: UNREACHABLE();
                    return 0;
                }
            }
        }

        value * inv_rf(rational_function_value * a) {
            polynomial const & an = a->num();
            polynomial const & ad = a->den();
            rational_function_value * r = mk_rational_function_value_core(a->ext(), ad.size(), ad.c_ptr(), an.size(), an.c_ptr());
            bqim().inv(interval(a), r->interval());
            SASSERT(!contains_zero(r->interval()));
            return r;
        }

        value * inv(value * a) {
            if (a == 0) {
                throw exception("division by zero");
            }
            if (is_nz_rational(a)) {
                scoped_mpq r(qm());
                qm().inv(to_mpq(a), r);
                return mk_rational(r);
            }
            else {
                return inv_rf(to_rational_function(a));
            }
        }

        void set(numeral & n, value * v) {
            inc_ref(v);
            dec_ref(n.m_value);
            n.m_value = v;
        }

        void neg(numeral & a) {
            set(a, neg(a.m_value));
        }

        void neg(numeral const & a, numeral & b) {
            set(b, neg(a.m_value));
        }

        void inv(numeral & a) {
            set(a, inv(a.m_value));
        }

        void inv(numeral const & a, numeral & b) {
            set(b, inv(a.m_value));
        }

        void add(numeral const & a, numeral const & b, numeral & c) {
            set(c, add(a.m_value, b.m_value));
        }

        void sub(numeral const & a, numeral const & b, numeral & c) {
            set(c, sub(a.m_value, b.m_value));
        }

        void mul(numeral const & a, numeral const & b, numeral & c) {
            set(c, mul(a.m_value, b.m_value));
        }
        
        void div(numeral const & a, numeral const & b, numeral & c) {
            set(c, div(a.m_value, b.m_value));
        }

        int compare(value * a, value * b) {
            if (a == 0)
                return -sign(b);
            else if (b == 0)
                return sign(a);
            else if (is_nz_rational(a) && is_nz_rational(b))
                return qm().lt(to_mpq(a), to_mpq(b)) ? -1 : 1;
            else {
                // TODO: try to refine interval before switching to sub+sign approach
                if (bqim().before(interval(a), interval(b)))
                    return -1;
                else if (bqim().before(interval(b), interval(a)))
                    return 1;
                else {
                    value_ref diff(*this);
                    diff = sub(a, b);
                    return sign(diff);
                }
            }
        }

        int compare(numeral const & a, numeral const & b) {
            return compare(a.m_value, b.m_value);
        }

        void select(numeral const & prev, numeral const & next, numeral & result) {
            // TODO
        }

        struct collect_algebraic_refs {
            char_vector            m_visited; // Set of visited algebraic extensions.
            ptr_vector<algebraic>  m_found;   // vector/list of visited algebraic extensions.

            void mark(extension * ext) {
                if (ext->is_algebraic()) {
                    m_visited.reserve(ext->idx() + 1, false);
                    if (!m_visited[ext->idx()]) {
                        m_visited[ext->idx()] = true;
                        algebraic * a = to_algebraic(ext);
                        m_found.push_back(a);
                        mark(a->p());
                    }
                }
            }

            void mark(polynomial const & p) {
                for (unsigned i = 0; i < p.size(); i++) {
                    mark(p[i]);
                }
            }
            
            void mark(value * v) {
                if (v == 0 || is_nz_rational(v))
                    return;
                rational_function_value * rf = to_rational_function(v);
                mark(rf->ext());
                mark(rf->num());
                mark(rf->den());
            }
        };

        bool use_parenthesis(value * v) const {
            if (is_zero(v) || is_nz_rational(v)) 
                return false;
            rational_function_value * rf = to_rational_function(v);
            return rf->num().size() > 1 || !is_rational_one(rf->den());
        }

        template<typename DisplayVar>
        void display_polynomial(std::ostream & out, polynomial const & p, DisplayVar const & display_var, bool compact) const {
            unsigned i = p.size();
            bool first = true;
            SASSERT(i > 0);
            while (i > 0) {
                --i;
                if (p[i] == 0)
                    continue;
                if (first)
                    first = false;
                else
                    out << " + ";
                if (i == 0)
                    display(out, p[i], compact);
                else {
                    if (!is_rational_one(p[i])) {
                        if (use_parenthesis(p[i])) {
                            out << "(";
                            display(out, p[i], compact);
                            out << ")*";
                        }
                        else {
                            display(out, p[i], compact);
                            out << "*";
                        }
                    }
                    display_var(out, compact);
                    if (i > 1)
                        out << "^" << i;
                }
            }
        }

        struct display_free_var_proc {
            void operator()(std::ostream & out, bool compact) const {
                out << "#";
            }
        };

        struct display_ext_proc {
            imp const &  m;
            extension *  m_ref;    
            display_ext_proc(imp const & _m, extension * r):m(_m), m_ref(r) {}
            void operator()(std::ostream & out, bool compact) const {
                m.display_ext(out, m_ref, compact);
            }
        };

        void display_polynomial_expr(std::ostream & out, polynomial const & p, extension * ext, bool compact) const {
            display_polynomial(out, p, display_ext_proc(*this, ext), compact);
        }

        static void display_poly_sign(std::ostream & out, int s) {
            if (s < 0)
                out << " < 0";
            else if (s == 0)
                out << " = 0";
            else
                out << " > 0";
        }

        void display_algebraic_def(std::ostream & out, algebraic * a, bool compact) const {
            out << "root(";
            display_polynomial(out, a->p(), display_free_var_proc(), compact);
            out << ", ";
            bqim().display(out, a->interval());
            out << ", {";
            signs const & s = a->s();
            for (unsigned i = 0; i < s.size(); i++) {
                if (i > 0)
                    out << ", ";
                display_polynomial(out, s[i].first, display_free_var_proc(), compact);
                display_poly_sign(out, s[i].second);
            }
            out << "})";
        }

        void display_ext(std::ostream & out, extension * r, bool compact) const {
            switch (r->knd()) {
            case extension::TRANSCENDENTAL: to_transcendental(r)->display(out); break;
            case extension::INFINITESIMAL:  to_infinitesimal(r)->display(out); break;
            case extension::ALGEBRAIC: 
                if (compact)
                    out << "r!" << r->idx();
                else
                    display_algebraic_def(out, to_algebraic(r), compact);
            }
        }

        void display(std::ostream & out, value * v, bool compact) const {
            if (v == 0)
                out << "0";
            else if (is_nz_rational(v)) 
                qm().display(out, to_mpq(v));
            else {
                rational_function_value * rf = to_rational_function(v);
                if (is_rational_one(rf->den())) {
                    display_polynomial_expr(out, rf->num(), rf->ext(), compact);
                }
                else if (is_rational_one(rf->num())) {
                    out << "1/(";
                    display_polynomial_expr(out, rf->den(), rf->ext(), compact);
                    out << ")";
                }
                else {
                    out << "(";
                    display_polynomial_expr(out, rf->num(), rf->ext(), compact);
                    out << ")/(";
                    display_polynomial_expr(out, rf->den(), rf->ext(), compact);
                    out << ")";
                }
            }
        }

        void display_compact(std::ostream & out, numeral const & a) const {
            collect_algebraic_refs c;
            c.mark(a.m_value);
            if (c.m_found.empty()) {
                display(out, a.m_value, true);
            }
            else {
                std::sort(c.m_found.begin(), c.m_found.end(), rank_lt_proc());
                out << "[";
                display(out, a.m_value, true);
                for (unsigned i = 0; i < c.m_found.size(); i++) {
                    algebraic * ext = c.m_found[i];
                    out << ", r!" << ext->idx() << " = ";
                    display_algebraic_def(out, ext, true);
                }
                out << "]";
            }
        }
        
        void display(std::ostream & out, numeral const & a) const {
            display(out, a.m_value, false);
        }

        void display_non_rational_in_decimal(std::ostream & out, numeral const & a, unsigned precision) {
            SASSERT(!is_zero(a));
            SASSERT(!is_nz_rational(a));
            mpbqi const & i = interval(a.m_value);
            if (refine_interval(a.m_value, precision*4)) {
                // hack
                if (bqm().is_int(i.lower())) 
                    bqm().display_decimal(out, i.upper(), precision);
                else
                    bqm().display_decimal(out, i.lower(), precision);
            }
            else {
                if (sign(a.m_value) > 0)
                    out << "?";
                else
                    out << "-?";
            }
        }
        
        void display_decimal(std::ostream & out, numeral const & a, unsigned precision) const {
            if (is_zero(a)) {
                out << "0";
            }
            else if (is_nz_rational(a)) {
                qm().display_decimal(out, to_mpq(a), precision);
            }
            else {
                const_cast<imp*>(this)->display_non_rational_in_decimal(out, a, precision);
            }
        }

        void display_interval(std::ostream & out, numeral const & a) const {
            if (is_zero(a))
                out << "[0, 0]";
            else
                bqim().display(out, interval(a.m_value));
        }
    };

    // Helper object for restoring the value intervals.
    class save_interval_ctx {
        manager::imp * m;
    public:
        save_interval_ctx(manager const * _this):m(_this->m_imp) { SASSERT (m); }
        ~save_interval_ctx() { m->restore_saved_intervals(); }
    };

    manager::manager(unsynch_mpq_manager & m, params_ref const & p, small_object_allocator * a) {
        m_imp = alloc(imp, m, p, a);
    }
        
    manager::~manager() {
        dealloc(m_imp);
    }

    void manager::get_param_descrs(param_descrs & r) {
        // TODO
    }

    void manager::set_cancel(bool f) {
        m_imp->set_cancel(f);
    }

    void manager::updt_params(params_ref const & p) {
        m_imp->updt_params(p);
    }

    unsynch_mpq_manager & manager::qm() const {
        return m_imp->m_qm;
    }

    void manager::del(numeral & a) {
        m_imp->del(a);
    }

    void manager::mk_infinitesimal(char const * n, numeral & r) {
        m_imp->mk_infinitesimal(n, r);
    }

    void manager::mk_infinitesimal(numeral & r) {
        m_imp->mk_infinitesimal(r);
    }
        
    void manager::mk_transcendental(char const * n, mk_interval & proc, numeral & r) {
        m_imp->mk_transcendental(n, proc, r);
    }

    void manager::mk_transcendental(mk_interval & proc, numeral & r) {
        m_imp->mk_transcendental(proc, r);
    }

    void manager::mk_pi(numeral & r) {
        m_imp->mk_pi(r);
    }

    void manager::mk_e(numeral & r) {
        m_imp->mk_e(r);
    }

    void manager::isolate_roots(unsigned n, numeral const * as, numeral_vector & roots) {
        save_interval_ctx ctx(this);
        m_imp->isolate_roots(n, as, roots);
    }

    void manager::reset(numeral & a) {
        m_imp->reset(a);
    }

    int manager::sign(numeral const & a) {
        save_interval_ctx ctx(this);
        return m_imp->sign(a);
    }
        
    bool manager::is_zero(numeral const & a) {
        return sign(a) == 0;
    }

    bool manager::is_pos(numeral const & a) {
        return sign(a) > 0;
    }

    bool manager::is_neg(numeral const & a) {
        return sign(a) < 0;
    }

    bool manager::is_int(numeral const & a) {
        return m_imp->is_int(a);
    }
        
    bool manager::is_real(numeral const & a) {
        return m_imp->is_real(a);
    }
        
    void manager::set(numeral & a, int n) {
        m_imp->set(a, n);
    }

    void manager::set(numeral & a, mpz const & n) {
        m_imp->set(a, n);
    }

    void manager::set(numeral & a, mpq const & n) {
        m_imp->set(a, n);
    }

    void manager::set(numeral & a, numeral const & n) {
        m_imp->set(a, n);
    }

    void manager::swap(numeral & a, numeral & b) {
        std::swap(a.m_value, b.m_value);
    }

    void manager::root(numeral const & a, unsigned k, numeral & b) {
        save_interval_ctx ctx(this);
        m_imp->root(a, k, b);
    }
      
    void manager::power(numeral const & a, unsigned k, numeral & b) {
        save_interval_ctx ctx(this);
        m_imp->power(a, k, b);
    }

    void manager::add(numeral const & a, numeral const & b, numeral & c) {
        save_interval_ctx ctx(this);
        m_imp->add(a, b, c);
    }

    void manager::add(numeral const & a, mpz const & b, numeral & c) {
        scoped_numeral _b(*this);
        set(_b, b);
        add(a, _b, c);
    }

    void manager::sub(numeral const & a, numeral const & b, numeral & c) {
        save_interval_ctx ctx(this);
        m_imp->sub(a, b, c);
    }

    void manager::mul(numeral const & a, numeral const & b, numeral & c) {
        save_interval_ctx ctx(this);
        m_imp->mul(a, b, c);
    }

    void manager::neg(numeral & a) {
        save_interval_ctx ctx(this);
        m_imp->neg(a);
    }

    void manager::neg(numeral const & a, numeral & b) {
        save_interval_ctx ctx(this);
        m_imp->neg(a, b);
    }

    void manager::inv(numeral & a) {
        save_interval_ctx ctx(this);
        m_imp->inv(a);
    }

    void manager::inv(numeral const & a, numeral & b) {
        save_interval_ctx ctx(this);
        m_imp->inv(a, b);
    }

    void manager::div(numeral const & a, numeral const & b, numeral & c) {
        save_interval_ctx ctx(this);
        m_imp->div(a, b, c);
    }

    int manager::compare(numeral const & a, numeral const & b) {
        save_interval_ctx ctx(this);
        return m_imp->compare(a, b);
    }

    bool manager::eq(numeral const & a, numeral const & b) {
        return compare(a, b) == 0;
    }

    bool manager::eq(numeral const & a, mpq const & b) {
        scoped_numeral _b(*this);
        set(_b, b);
        return eq(a, _b);
    }

    bool manager::eq(numeral const & a, mpz const & b) {
        scoped_numeral _b(*this);
        set(_b, b);
        return eq(a, _b);
    }

    bool manager::lt(numeral const & a, numeral const & b) {
        return compare(a, b) < 0;
    }

    bool manager::lt(numeral const & a, mpq const & b) {
        scoped_numeral _b(*this);
        set(_b, b);
        return lt(a, _b);
    }

    bool manager::lt(numeral const & a, mpz const & b) {
        scoped_numeral _b(*this);
        set(_b, b);
        return lt(a, _b);
    }

    bool manager::gt(numeral const & a, mpq const & b) {
        scoped_numeral _b(*this);
        set(_b, b);
        return gt(a, _b);
    }

    bool manager::gt(numeral const & a, mpz const & b) {
        scoped_numeral _b(*this);
        set(_b, b);
        return gt(a, _b);
    }

    void manager::select(numeral const & prev, numeral const & next, numeral & result) {
        save_interval_ctx ctx(this);
        m_imp->select(prev, next, result);
    }
        
    void manager::display(std::ostream & out, numeral const & a) const {
        save_interval_ctx ctx(this);
        m_imp->display(out, a);
    }

    void manager::display_decimal(std::ostream & out, numeral const & a, unsigned precision) const {
        save_interval_ctx ctx(this);
        m_imp->display_decimal(out, a, precision);
    }

    void manager::display_interval(std::ostream & out, numeral const & a) const {
        save_interval_ctx ctx(this);
        m_imp->display_interval(out, a);
    }

};

void pp(realclosure::manager::imp * imp, realclosure::polynomial const & p, realclosure::extension * ext) {
    imp->display_polynomial_expr(std::cout, p, ext, false);
    std::cout << std::endl;
}

void pp(realclosure::manager::imp * imp, realclosure::value * v) {
    imp->display(std::cout, v, false);
    std::cout << std::endl;
}

void pp(realclosure::manager::imp * imp, unsigned sz, realclosure::value * const * p) {
    for (unsigned i = 0; i < sz; i++)
        pp(imp, p[i]);
}

void pp(realclosure::manager::imp * imp, realclosure::manager::imp::value_ref_buffer const & p) {
    for (unsigned i = 0; i < p.size(); i++)
        pp(imp, p[i]);
}

void pp(realclosure::manager::imp * imp, realclosure::mpbqi const & i) {
    imp->bqim().display(std::cout, i);
    std::cout << std::endl;
}

void pp(realclosure::manager::imp * imp, realclosure::manager::imp::scoped_mpqi const & i) {
    imp->qim().display(std::cout, i);
    std::cout << std::endl;
}

void pp(realclosure::manager::imp * imp, mpbq const & n) {
    imp->bqm().display(std::cout, n);
    std::cout << std::endl;
}

void pp(realclosure::manager::imp * imp, mpq const & n) {
    imp->qm().display(std::cout, n);
    std::cout << std::endl;
}
