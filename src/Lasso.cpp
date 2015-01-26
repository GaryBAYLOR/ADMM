#include "ADMMLasso.h"
#include "DataStd.h"

using Eigen::MatrixXd;
using Eigen::VectorXd;
using Eigen::ArrayXd;
using Eigen::ArrayXXd;

using Rcpp::wrap;
using Rcpp::as;
using Rcpp::List;
using Rcpp::IntegerVector;
using Rcpp::NumericVector;
using Rcpp::NumericMatrix;
using Rcpp::Environment;
using Rcpp::Function;
using Rcpp::Named;

typedef Eigen::Map<MatrixXd> MapMat;
typedef Eigen::Map<VectorXd> MapVec;
typedef Eigen::Map<ArrayXd>  MapArray;
typedef Eigen::SparseVector<double> SpVec;
typedef Eigen::SparseMatrix<double> SpMat;

// calculating the spectral radius of X'X, i.e., the largest eigenvalue
inline double max_eigenvalue(const MatrixXd &X)
{
    NumericMatrix x = wrap(X);
    Environment rARPACK = Environment::namespace_env("rARPACK");
    Function svds = rARPACK["svds"];
    IntegerVector k = IntegerVector::create(1);
    IntegerVector nu = IntegerVector::create(0);
    IntegerVector nv = nu;
    List res = svds(x, k, nu, nv);
    double sprad = as<double>(res["d"]);
    return sprad * sprad;
}

inline void write_beta_matrix(SpMat &betas, int col, double beta0, SpVec &coef)
{
    betas.insert(0, col) = beta0;

    for(SpVec::InnerIterator iter(coef); iter; ++iter)
    {
        betas.insert(iter.index() + 1, col) = iter.value();
    }
}

RcppExport SEXP admm_lasso(SEXP x_, SEXP y_, SEXP lambda_,
                           SEXP nlambda_, SEXP lmin_ratio_,
                           SEXP standardize_, SEXP intercept_,
                           SEXP opts_)
{
BEGIN_RCPP

    MatrixXd datX(as<MatrixXd>(x_));
    VectorXd datY(as<VectorXd>(y_));
    
    // In glmnet, we minimize
    //   1/(2n) * ||y - X * beta||^2 + lambda * ||beta||_1
    // which is equivalent to minimizing
    //   1/2 * ||y - X * beta||^2 + n * lambda * ||beta||_1
    int n = datX.rows();
    int p = datX.cols();
    ArrayXd lambda(as<ArrayXd>(lambda_));
    int nlambda = lambda.size();
    
    List opts(opts_);
    int maxit = as<int>(opts["maxit"]);
    double eps_abs = as<double>(opts["eps_abs"]);
    double eps_rel = as<double>(opts["eps_rel"]);
    double rho_ratio = as<double>(opts["rho_ratio"]);

    bool standardize = as<bool>(standardize_);
    bool intercept = as<bool>(intercept_);
    DataStd datstd(n, p, standardize, intercept);
    datstd.standardize(datX, datY);

    double sprad = max_eigenvalue(datX);
    
    ADMMLasso solver(datX, datY, sprad, eps_abs, eps_rel);
    if(nlambda < 1)
    {
        double lmax = solver.lambda_max() / n * datstd.get_scaleY();
        double lmin = as<double>(lmin_ratio_) * lmax;
        lambda.setLinSpaced(as<int>(nlambda_), log(lmax), log(lmin));
        lambda = lambda.exp();
        nlambda = lambda.size();
    }

    SpMat beta(p + 1, nlambda);
    beta.reserve(Eigen::VectorXi::Constant(nlambda, std::min(n, p)));

    IntegerVector niter(nlambda);
    double ilambda = 0.0;

    for(int i = 0; i < nlambda; i++)
    {
        ilambda = lambda[i] * n / datstd.get_scaleY();
        if(i == 0)
            solver.init(ilambda, ilambda / (rho_ratio * sprad));
        else
            solver.init_warm(ilambda);

        niter[i] = solver.solve(maxit);
        SpVec res = solver.get_x();
        double beta0 = 0.0;
        datstd.recover(beta0, res);
        write_beta_matrix(beta, i, beta0, res);
    }

    beta.makeCompressed();

    return List::create(Named("lambda") = lambda,
                        Named("beta") = beta,
                        Named("niter") = niter);

END_RCPP
}
