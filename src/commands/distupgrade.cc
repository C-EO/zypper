/*---------------------------------------------------------------------------*\
                          ____  _ _ __ _ __  ___ _ _
                         |_ / || | '_ \ '_ \/ -_) '_|
                         /__|\_, | .__/ .__/\___|_|
                             |__/|_|  |_|
\*---------------------------------------------------------------------------*/
#include "distupgrade.h"
#include "utils/flags/flagtypes.h"
#include "commands/conditions.h"
#include "solve-commit.h"
#include "utils/messages.h"
#include "commonflags.h"

#include "Zypper.h"

DistUpgradeCmd::DistUpgradeCmd(std::vector<std::string> &&commandAliases_r ) :
  ZypperBaseCommand (
    std::move( commandAliases_r ),
    // translators: command synopsis; do not translate lowercase words
    _("dist-upgrade (dup) [OPTIONS]"),
    // translators: command summary: dist-upgrade, dup
    _("Perform a distribution upgrade."),
    // translators: command description
    _("Perform a distribution upgrade."),
    ResetRepoManager | InitTarget | InitRepos | LoadResolvables
  )
{
  _dryRunOpts.setCompatibilityMode( CompatModeBits::EnableNewOpt | CompatModeBits::EnableRugOpt );
}

zypp::ZyppFlags::CommandGroup DistUpgradeCmd::cmdOptions() const
{
  auto that = const_cast<DistUpgradeCmd *>(this);
  auto & set = SolverSettings::instanceNoConst();
  return zypp::ZyppFlags::CommandGroup({
    { "from", '\0', ZyppFlags::Repeatable | ZyppFlags::RequiredArgument, ZyppFlags::StringVectorType( &DupSettings::instanceNoConst()._fromRepos, ARG_REPOSITORY),
        // translators: --from <ALIAS|#|URI>
        _("Restrict upgrade to specified repository.")
    },
    { "remove-orphaned", '\0', ZyppFlags::NoArgument, ZyppFlags::TriBoolType( set._removeOrphaned, ZyppFlags::StoreTrue ),
      _("Remove unneeded orphaned packages.")
    },
    CommonFlags::detailsFlag( that->_details )
  });
}

int DistUpgradeCmd::systemSetup( Zypper &zypper )
{
  // bsc#1228434, bsc#1236939, bsc#961719, bsc#961724, et.al.
  // dup must not continue if repositories failed to refresh.
  SetupSystemFlags flags = DefaultSetup | FailIfReposFail;
  int res = defaultSystemSetup( zypper, flags );
  if ( res == ZYPPER_EXIT_INF_REPOS_SKIPPED ) {
    // FailIfReposFail
    zypper.out().taggedPar( 4, MSG_ERRORString( command()[0]+":" ), _("Due to the treatment of orphaned packages dist-upgrade depends on a proper repository setup more than any other command. It must not continue if enabled repositories fail to refresh. This may severely damage the system. If a failing repository is actually not needed, it must be disabled.")
      +std::string(" ")
      +_("See 'man zypper' for more information about this command.") );
    zypper.setExitCode( ZYPPER_EXIT_ERR_ZYPP );
  }
  return zypper.exitCode();
}

void DistUpgradeCmd::doReset()
{
  DupSettings::reset();
  _details = false;
}

std::vector<BaseCommandConditionPtr> DistUpgradeCmd::conditions() const
{
  return {
    std::make_shared<NeedsRootCondition>(),
    std::make_shared<NeedsWritableRoot>()
  };
}

int DistUpgradeCmd::execute( Zypper &zypper, const std::vector<std::string> &positionalArgs_r )
{
  // too many arguments
  if (positionalArgs_r.size() > 0)
  {
    report_too_many_arguments( help() );
    return ( ZYPPER_EXIT_ERR_INVALID_ARGS );
  }

  if ( !InitRepoSettings::instance()._repoFilter.size() && !DupSettings::instance()._fromRepos.size() && zypper.repoManager().knownRepositories().size() > 1 ) {
    zypper.out().warning( str::form(
      _("You are about to do a distribution upgrade with all enabled"
        " repositories. Make sure these repositories are compatible before you"
        " continue. See '%s' for more information about this command."),
        "man zypper"));
  }

  Summary::ViewOptions viewOpts = ( Summary::ViewOptions ) ( Summary::ViewOptions::DEFAULT | Summary::ViewOptions::SHOW_LOCKS );
  if ( _details ) {
    viewOpts = ( Summary::ViewOptions ) ( viewOpts | Summary::ViewOptions::DETAILS );
  }

  solve_and_commit( zypper, SolveAndCommitPolicy( ).summaryOptions( viewOpts ).downloadMode( _downloadModeOpts.mode() ) );
  return zypper.exitCode();
}
