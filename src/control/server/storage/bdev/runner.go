//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"fmt"
	"os"
	"os/exec"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	spdkSetupPath      = "../share/daos/control/setup_spdk.sh"
	defaultNrHugepages = 4096
	nrHugepagesEnv     = "_NRHUGE"
	targetUserEnv      = "_TARGET_USER"
	pciAllowListEnv    = "_PCI_ALLOWED"
	pciBlockListEnv    = "_PCI_BLOCKED"
	driverOverrideEnv  = "_DRIVER_OVERRIDE"
	vfioDisabledDriver = "uio_pci_generic"
)

type runCmdFn func(logging.Logger, []string, string, ...string) (string, error)

type runCmdError struct {
	wrapped error
	stdout  string
}

func (rce *runCmdError) Error() string {
	if ee, ok := rce.wrapped.(*exec.ExitError); ok {
		return fmt.Sprintf("%s: stdout: %s; stderr: %s", ee.ProcessState,
			rce.stdout, ee.Stderr)
	}

	return fmt.Sprintf("%s: stdout: %s", rce.wrapped.Error(), rce.stdout)
}

func run(log logging.Logger, env []string, cmdStr string, args ...string) (string, error) {
	if os.Geteuid() != 0 {
		return "", errors.New("must be run with root privileges")
	}

	var cmdPath string
	var err error
	if cmdPath, err = exec.LookPath(cmdStr); err != nil {
		cmdPath, err = common.GetAdjacentPath(cmdStr)
		if err != nil {
			return "", errors.Wrapf(err, "unable to resolve path to %s", cmdStr)
		}
		if _, err := os.Stat(cmdPath); err != nil {
			return "", err
		}
	}

	log.Debugf("running script: %s", cmdPath)
	cmd := exec.Command(cmdPath, args...)
	cmd.Env = env
	out, err := cmd.CombinedOutput()
	if err != nil {
		return "", &runCmdError{
			wrapped: err,
			stdout:  string(out),
		}
	}

	return string(out), nil
}

type spdkSetupScript struct {
	log        logging.Logger
	scriptPath string
	runCmd     runCmdFn
}

func defaultScriptRunner(log logging.Logger) *spdkSetupScript {
	return &spdkSetupScript{
		log:        log,
		scriptPath: spdkSetupPath,
		runCmd:     run,
	}
}

// Prepare executes setup script to allocate hugepages and unbind PCI devices
// (that don't have active mountpoints) from generic kernel driver to be
// used with SPDK. Either all PCI devices will be unbound by default if allow list
// parameter is not set, otherwise PCI devices can be specified by passing in a
// allow list of PCI addresses.
//
// NOTE: will make the controller disappear from /dev until reset() called.
func (s *spdkSetupScript) Prepare(req *storage.BdevPrepareRequest) error {
	nrHugepages := req.HugePageCount
	if nrHugepages <= 0 {
		nrHugepages = defaultNrHugepages
	}

	env := []string{
		fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
		fmt.Sprintf("%s=%d", nrHugepagesEnv, nrHugepages),
		fmt.Sprintf("%s=%s", targetUserEnv, req.TargetUser),
	}
	if req.PCIAllowList != "" {
		env = append(env, fmt.Sprintf("%s=%s", pciAllowListEnv, req.PCIAllowList))
	}
	if req.PCIBlockList != "" {
		env = append(env, fmt.Sprintf("%s=%s", pciBlockListEnv, req.PCIBlockList))
	}
	if req.DisableVFIO {
		env = append(env, fmt.Sprintf("%s=%s", driverOverrideEnv, vfioDisabledDriver))
	}

	s.log.Debugf("spdk setup env: %v", env)
	out, err := s.runCmd(s.log, env, s.scriptPath) // -> func run(log logging.Logger, env []string, cmdStr string, args ...string)
	s.log.Debugf("spdk setup stdout:\n%s\n", out)

	return errors.Wrapf(err, "spdk setup failed (%s)", out)
}

// Reset executes setup script to reset hugepage allocations and unbind PCI devices
// (that don't have active mountpoints) from SPDK compatible driver e.g. VFIO and
// bind back to the kernel bdev driver to be used by the OS. Either all PCI devices
// will be unbound by default if allow list parameter is not set, otherwise PCI
// devices can be specified by passing in a allow list of PCI addresses.
//
// NOTE: will make the controller reappear in /dev.
func (s *spdkSetupScript) Reset(req *storage.BdevPrepareRequest) error {
	env := []string{
		fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
	}
	if req.PCIAllowList != "" {
		env = append(env, fmt.Sprintf("%s=%s", pciAllowListEnv, req.PCIAllowList))
	}
	if req.PCIBlockList != "" {
		env = append(env, fmt.Sprintf("%s=%s", pciBlockListEnv, req.PCIBlockList))
	}

	s.log.Debugf("spdk reset env: %v", env)
	out, err := s.runCmd(s.log, env, s.scriptPath, "reset")
	s.log.Debugf("spdk reset stdout:\n%s\n", out)

	return errors.Wrapf(err, "spdk reset failed (%s)", out)
}
