#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics


class IorIntercept(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR with and without interception.

       library on a single server and single client setting with
       basic parameters.

    :avocado: recursive
    """

    def test_ior_intercept(self):
        """Jira ID: DAOS-3498.

        Test Description:
            Purpose of this test is to run ior using dfuse for 5 minutes
            and capture the metrics and use the intercepiton library by
            exporting LD_PRELOAD to the libioil.so path and rerun the
            above ior and capture the metrics and compare the
            performance difference and check using interception
            library make significant performance improvement.

        Use case:
            Run ior with read, write, CheckWrite, CheckRead
                for 5 minutes
            Run ior with read, write, CheckWrite, CheckRead
                for 5 minutes with interception library
            Compare the results and check whether using interception
                library provides better performance.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=daosio,dfuse,il
        :avocado: tags=iorinterceptbasic
        """
        apis = self.params.get("ior_api", '/run/ior/iorflags/ssf/*')
        for api in apis:
            self.ior_cmd.api.update(api)
            out = self.run_ior_with_pool(fail_on_warning=True)
            without_intercept = IorCommand.get_ior_metrics(out)
            if api == "POSIX":
                intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
                out = self.run_ior_with_pool(intercept, fail_on_warning=True)
                with_intercept = IorCommand.get_ior_metrics(out)
                max_mib = int(IorMetrics.Max_MiB)
                min_mib = int(IorMetrics.Min_MiB)
                mean_mib = int(IorMetrics.Mean_MiB)
                write_x = self.params.get("write_x",
                                          "/run/ior/iorflags/ssf/*", 1)
                read_x = self.params.get("read_x",
                                         "/run/ior/iorflags/ssf/*", 1)

                # Verifying write performance
                self.assertTrue(float(with_intercept[0][max_mib]) >
                                write_x * float(without_intercept[0][max_mib]))
                self.assertTrue(float(with_intercept[0][min_mib]) >
                                write_x * float(without_intercept[0][min_mib]))
                self.assertTrue(float(with_intercept[0][mean_mib]) >
                                write_x * float(without_intercept[0][mean_mib]))

                # Verifying read performance
                self.assertTrue(float(with_intercept[1][max_mib]) >
                                read_x * float(without_intercept[1][max_mib]))
                # DAOS-5857 There's a lot of volatility in this result, so disable it to reduce
                # testing noise.  This test runs IOR with multiple iterations so it should only
                # affect min results, mean and max results should be more resilient.
                #self.assertTrue(float(with_intercept[1][min_mib]) >
                #                read_x * float(without_intercept[1][min_mib]))
                self.assertTrue(float(with_intercept[1][mean_mib]) >
                                read_x * float(without_intercept[1][mean_mib]))
