#include <iostream>

#include <zypp/ZYpp.h> // for ResPool::instance()

#include <zypp/base/Logger.h>
#include <zypp/base/Algorithm.h>
#include <zypp/Patch.h>
#include <zypp/Pattern.h>
#include <zypp/Product.h>
#include <zypp/sat/Solvable.h>

#include <zypp/PoolItem.h>
#include <zypp/PoolQuery.h>
#include <zypp/ResPoolProxy.h>
#include <zypp/ui/SelectableTraits.h>

#include "main.h"
#include "utils/misc.h"

#include "search.h"

extern ZYpp::Ptr God;

FillSearchTableSolvable::FillSearchTableSolvable( Table & table, TriBool inst_notinst )
: _table( &table )
, _inst_notinst( inst_notinst )
{
  Zypper & zypper( Zypper::instance() );
  if ( zypper.cOpts().find("repo") != zypper.cOpts().end() )
  {
    std::list<RepoInfo> & repos( zypper.runtimeData().repos );
    for_( it, repos.begin(), repos.end() )
      _repos.insert( it->alias() );
  }

  //
  // *** CAUTION: It's a mess, but adding/changing colums here requires
  //              adapting OutXML::searchResult !
  //
  *_table << ( TableHeader()
	  // translators: S for 'installed Status'
	  << _("S")
	  // translators: name (general header)
	  << _("Name")
	  // translators: type (general header)
	  << _("Type")
	  // translators: package version (header)
	  << table::EditionStyleSetter( *_table, _("Version") )
	  // translators: package architecture (header)
	  << _("Arch")
	  // translators: package's repository (header)
	  << _("Repository") );
}

bool FillSearchTableSolvable::addPicklistItem( const ui::Selectable::constPtr & sel, const PoolItem & pi ) const
{
  // --repo => we only want the repo resolvables, not @System (bnc #467106)
  if ( !_repos.empty() && _repos.find( pi->repoInfo().alias() ) == _repos.end() )
    return false;

  // hide patterns with user visible flag not set (bnc #538152)
  if ( pi->isKind<Pattern>() && ! pi->asKind<Pattern>()->userVisible() )
    return false;

  // On the fly filter unwanted according to _inst_notinst
  const char *statusIndicator = nullptr;
  if ( indeterminate(_inst_notinst)  )
    statusIndicator = computeStatusIndicator( pi, sel );
  else
  {
    bool iType;
    statusIndicator = computeStatusIndicator( pi, sel, &iType );
    if ( (bool)_inst_notinst != iType )
      return false;
  }

  TableRow row;
  row
    << statusIndicator
    << pi->name()
    << kind_to_string_localized( pi->kind(), 1 )
    << pi->edition().asString()
    << pi->arch().asString()
    << ( pi->isSystem()
       ? (std::string("(") + _("System Packages") + ")")
       : pi->repository().asUserString() );

  *_table << row;
  return true;	// actually added a row
}

//
// PoolQuery iterator as argument provides information about matches
//
bool FillSearchTableSolvable::operator()( const PoolQuery::const_iterator & it ) const
{
  // all FillSearchTableSolvable::operator()( const PoolItem & pi )
  if ( ! operator()(*it) )
    return false;	// no row was added due to filter

  // after addPicklistItem( const ui::Selectable::constPtr & sel, const PoolItem & pi ) is
  // done, add the details about matches to last row
  TableRow & lastRow( _table->rows().back() );

  // don't show details for patterns with user visible flag not set (bnc #538152)
  if ( it->kind() == ResKind::pattern )
  {
    Pattern::constPtr ptrn = asKind<Pattern>(*it);
    if ( ptrn && !ptrn->userVisible() )
      return true;
  }

  if ( !it.matchesEmpty() )
  {
    for_( match, it.matchesBegin(), it.matchesEnd() )
    {
      std::string attrib( match->inSolvAttr().asString() );
      if ( str::startsWith( attrib, "solvable:" ) )	// strip 'solvable:' from attribute
	attrib.erase( 0, 9 );

      if ( match->inSolvAttr() == sat::SolvAttr::summary ||
           match->inSolvAttr() == sat::SolvAttr::description )
      {
	// multiline matchstring
        lastRow.addDetail( attrib + ":" );
        lastRow.addDetail( match->asString() );
      }
      else
      {
        // print attribute and match in one line, e.g. requires: libzypp >= 11.6.2
        lastRow.addDetail( attrib + ": " + match->asString() );
      }
    }
  }
  return true;
}

bool FillSearchTableSolvable::operator()( const PoolItem & pi ) const
{
  ui::Selectable::constPtr sel( ui::Selectable::get( pi ) );
  return addPicklistItem( sel, pi );
}

bool FillSearchTableSolvable::operator()( sat::Solvable solv ) const
{ return operator()( PoolItem( solv ) ); }

bool FillSearchTableSolvable::operator()( const ui::Selectable::constPtr & sel ) const
{
  bool ret = false;
  // picklist: available items list prepended by those installed items not identicalAvailable
  for_( it, sel->picklistBegin(), sel->picklistEnd() )
  {
    if ( addPicklistItem( sel, *it ) || !ret )
      ret = true;	// at least one row added
  }
  return ret;
}


FillSearchTableSelectable::FillSearchTableSelectable( Table & table, TriBool installed_only )
: _table( &table )
, inst_notinst( installed_only )
{
  Zypper & zypper( Zypper::instance() );
  if ( zypper.cOpts().find("repo") != zypper.cOpts().end() )
  {
    std::list<RepoInfo> & repos( zypper.runtimeData().repos );
    for_( it, repos.begin(), repos.end() )
      _repos.insert( it->alias() );
  }

  TableHeader header;
  //
  // *** CAUTION: It's a mess, but adding/changing colums here requires
  //              adapting OutXML::searchResult !
  //
  // translators: S for installed Status
  header << _("S");
  header << _("Name");
  // translators: package summary (header)
  header << _("Summary");
  header << _("Type");
  *_table << header;
}

bool FillSearchTableSelectable::operator()( const ui::Selectable::constPtr & s ) const
{
  // hide patterns with user visible flag not set (bnc #538152)
  if ( s->kind() == ResKind::pattern )
  {
    Pattern::constPtr ptrn = s->candidateAsKind<Pattern>();
    if ( ptrn && !ptrn->userVisible() )
      return true;
  }


  // NOTE _tagForeign: This is a legacy issue:
  // - 'zypper search' always shows installed items as 'i'
  // - 'zypper search --repo X' shows 'foreign' installed items (the installed
  //    version is not provided by one of the enabled repos) as 'v'.
  bool _tagForeign = !_repos.empty();

  // On the fly filter unwanted according to inst_notinst
  const char *statusIndicator = nullptr;
  if ( indeterminate(inst_notinst)  )
    statusIndicator = computeStatusIndicator( *s, _tagForeign );
  else
  {
    bool iType;
    statusIndicator = computeStatusIndicator( *s, _tagForeign, &iType );
    if ( (bool)inst_notinst != iType )
      return true;
  }

  TableRow row;
  row << statusIndicator,
  row << s->name();
  row << s->theObj()->summary();
  row << kind_to_string_localized( s->kind(), 1 );
  *_table << row;
  return true;
}

///////////////////////////////////////////////////////////////////

static std::string string_weak_status( const ResStatus & rs )
{
  if ( rs.isRecommended() )
    return _("Recommended");
  if ( rs.isSuggested() )
    return _("Suggested");
  return "";
}


void list_patches( Zypper & zypper )
{
  MIL << "Pool contains " << God->pool().size() << " items. Checking whether available patches are needed." << std::endl;

  Table tbl;
  FillPatchesTable callback( tbl );
  invokeOnEach( God->pool().byKindBegin(ResKind::patch),
		God->pool().byKindEnd(ResKind::patch),
		callback);

  if ( tbl.empty() )
    zypper.out().info( _("No needed patches found.") );
  else
  {
    // display the result, even if --quiet specified
    tbl.sort();	// use default sort
    cout << tbl;
  }
}

static void list_patterns_xml( Zypper & zypper )
{
  cout << "<pattern-list>" << endl;

  bool repofilter = zypper.cOpts().count("repo");	// suppress @System if repo filter is on
  bool installed_only = zypper.cOpts().count("installed-only");
  bool notinst_only = zypper.cOpts().count("not-installed-only");

  for_( it, God->pool().byKindBegin<Pattern>(), God->pool().byKindEnd<Pattern>() )
  {
    bool isInstalled = it->status().isInstalled();
    if ( isInstalled && notinst_only && !installed_only )
      continue;
    if ( !isInstalled && installed_only && !notinst_only )
      continue;
    if ( repofilter && it->repository().info().name() == "@System" )
      continue;

    Pattern::constPtr pattern = asKind<Pattern>(it->resolvable());
    cout << asXML( *pattern, isInstalled ) << endl;
  }

  cout << "</pattern-list>" << endl;
}

static void list_pattern_table( Zypper & zypper)
{
  MIL << "Going to list patterns." << std::endl;

  Table tbl;

  // translators: S for installed Status
  tbl << ( TableHeader()
      << _("S")
      << _("Name")
      << table::EditionStyleSetter( tbl, _("Version") )
      << _("Repository")
      << _("Dependency") );

  bool repofilter = zypper.cOpts().count("repo");	// suppress @System if repo filter is on
  bool installed_only = zypper.cOpts().count("installed-only");
  bool notinst_only = zypper.cOpts().count("not-installed-only");

  for( const PoolItem & pi : God->pool().byKind<Pattern>() )
  {
    bool isInstalled = pi.status().isInstalled();
    if ( isInstalled && notinst_only && !installed_only )
      continue;
    else if ( !isInstalled && installed_only && !notinst_only )
      continue;

    const std::string & piRepoName( pi.repoInfo().name() );
    if ( repofilter && piRepoName == "@System" )
      continue;

    Pattern::constPtr pattern = asKind<Pattern>(pi.resolvable());
    // hide patterns with user visible flag not set (bnc #538152)
    if ( !pattern->userVisible() )
      continue;

    bool isLocked = pi.status().isLocked();
    tbl << ( TableRow()
	<< (isInstalled ? lockStatusTag( "i", isLocked, pi.identIsAutoInstalled() ) : lockStatusTag( "", isLocked ))
	<< pi.name()
	<< pi.edition()
	<< piRepoName
	<< string_weak_status(pi.status()) );
  }
  tbl.sort( 1 ); // Name

  if ( tbl.empty() )
    zypper.out().info(_("No patterns found.") );
  else
    // display the result, even if --quiet specified
    cout << tbl;
}

void list_patterns( Zypper & zypper )
{
  if ( zypper.out().type() == Out::TYPE_XML )
    list_patterns_xml( zypper );
  else
    list_pattern_table( zypper );
}

void list_packages( Zypper & zypper )
{
  MIL << "Going to list packages." << std::endl;
  Table tbl;

  const auto & copts( zypper.cOpts() );
  bool repofilter = copts.count("repo");	// suppress @System if repo filter is on
  bool installed_only = copts.count("installed-only");
  bool uninstalled_only = copts.count("not-installed-only");
  bool showInstalled = installed_only || !uninstalled_only;
  bool showUninstalled = uninstalled_only || !installed_only;

  bool orphaned = copts.count("orphaned");
  bool suggested = copts.count("suggested");
  bool recommended = copts.count("recommended");
  bool unneeded = copts.count("unneeded");
  bool check = ( orphaned || suggested || recommended || unneeded );
  if ( check )
  {
    God->resolver()->resolvePool();
  }
  auto checkStatus = [=]( ResStatus status_r )->bool {
    return ( ( orphaned && status_r.isOrphaned() )
	  || ( suggested && status_r.isSuggested() )
	  || ( recommended && status_r.isRecommended() )
	  || ( unneeded && status_r.isUnneeded() ) );
  };

  const auto & pproxy( God->pool().proxy() );
  for_( it, pproxy.byKindBegin(ResKind::package), pproxy.byKindEnd(ResKind::package) )
  {
    ui::Selectable::constPtr s = *it;
    // filter on selectable level
    if ( s->hasInstalledObj() )
    {
      if ( ! showInstalled )
	continue;
    }
    else
    {
      if ( ! showUninstalled )
	continue;
    }

    for_( it, s->picklistBegin(), s->picklistEnd() )
    {
      PoolItem pi = *it;
      if ( check )
      {
	// if checks are more detailed, show only matches
	// not whole selectables
	if ( pi.status().isInstalled() )
	{
	  if ( ! checkStatus( pi.status() ) )
	    continue;
	}
	else
	{
	  PoolItem ipi( s->identicalInstalledObj( pi ) );
	  if ( !ipi || !checkStatus( ipi.status() ) )
	    if ( ! checkStatus( pi.status() ) )
	      continue;
	}
      }

      const std::string & piRepoName( pi->repository().info().name() );
      if ( repofilter && piRepoName == "@System" )
	continue;

      TableRow row;
      row << computeStatusIndicator( pi, s )
          << piRepoName
          << pi->name()
          << pi->edition().asString()
          << pi->arch().asString();
      tbl << row;
    }
  }

  if ( tbl.empty() )
    zypper.out().info(_("No packages found.") );
  else
  {
    // display the result, even if --quiet specified
    tbl << ( TableHeader()
	// translators: S for installed Status
	<< _("S")
	<< _("Repository")
	<< _("Name")
	<< table::EditionStyleSetter( tbl, _("Version") )
	<< _("Arch") );

    if ( zypper.cOpts().count("sort-by-repo") )
      tbl.sort( 1 ); // Repo
    else
      tbl.sort( 2 ); // Name

    cout << tbl;
  }
}

static void list_products_xml( Zypper & zypper )
{
  bool repofilter = zypper.cOpts().count("repo");	// suppress @System if repo filter is on
  bool installed_only = zypper.cOpts().count("installed-only");
  bool notinst_only = zypper.cOpts().count("not-installed-only");

  cout << "<product-list>" << endl;
  for_( it, God->pool().byKindBegin(ResKind::product), God->pool().byKindEnd(ResKind::product) )
  {
    if ( it->status().isInstalled() && notinst_only )
      continue;
    else if ( !it->status().isInstalled() && installed_only )
      continue;
    if ( repofilter && it->repository().info().name() == "@System" )
      continue;
    Product::constPtr product = asKind<Product>(it->resolvable());
    cout << asXML( *product, it->status().isInstalled() ) << endl;
  }
  cout << "</product-list>" << endl;
}

// common product_table_row data
static void add_product_table_row( Zypper & zypper, TableRow & tr,  const Product::constPtr & product, bool forceShowAsBaseProduct_r = false )
{
  // repository
  tr << product->repoInfo().name();
  // internal (unix) name
  tr << product->name ();
  // full name (bnc #589333)
  tr << product->summary();
  // version
  tr << product->edition().asString();
  // architecture
  tr << product->arch().asString();
  // is base
  tr << asYesNo( forceShowAsBaseProduct_r || product->isTargetDistribution() );
}

static void list_product_table( Zypper & zypper )
{
  MIL << "Going to list products." << std::endl;

  Table tbl;

  tbl << ( TableHeader()
      // translators: S for installed Status
      << _("S")
      << _("Repository")
      // translators: used in products. Internal Name is the unix name of the
      // product whereas simply Name is the official full name of the product.
      << _("Internal Name")
      << _("Name")
      << table::EditionStyleSetter( tbl, _("Version") )
      << _("Arch")
      << _("Is Base") );

  bool repofilter = zypper.cOpts().count("repo");	// suppress @System if repo filter is on
  bool installed_only = zypper.cOpts().count("installed-only");
  bool notinst_only = zypper.cOpts().count("not-installed-only");

  for_( it, God->pool().proxy().byKindBegin(ResKind::product), God->pool().proxy().byKindEnd(ResKind::product) )
  {
    ui::Selectable::constPtr s = *it;

    // get the first installed object
    PoolItem installed;
    if ( !s->installedEmpty() )
      installed = s->installedObj();

    bool missedInstalled( installed ); // if no available hits, we need to print it

    // show available objects
    for_( it, s->availableBegin(), s->availableEnd() )
    {
      Product::constPtr product = asKind<Product>(it->resolvable());
      TableRow tr;
      PoolItem pi = *it;
      bool isLocked = pi.status().isLocked();
      bool forceShowAsBaseProduct = false;

      if ( installed )
      {
        if ( missedInstalled && identical( installed, pi ) )
        {
          if ( notinst_only )
            continue;
          tr << lockStatusTag( "i", isLocked, pi.identIsAutoInstalled() );
          // isTargetDistribution (i.e. is Base Product) needs to be taken from the installed item!
          forceShowAsBaseProduct = installed->asKind<Product>()->isTargetDistribution();
          missedInstalled = false;
	  // bnc#841473: Downside of reporting the installed product (repo: @System)
	  // instead of the available one (repo the product originated from) is that
	  // you see multiple identical '@System' entries if multiple repos contain
	  // the same product. Thus don't report again, if !missedInstalled.
        }
        else
        {
          if ( installed_only )
            continue;
          tr << lockStatusTag( "v", isLocked );
        }
      }
      else
      {
        if ( installed_only )
          continue;
        tr << lockStatusTag( "", isLocked );
      }
      add_product_table_row( zypper, tr, product, forceShowAsBaseProduct );
      tbl << tr;
    }

    if ( missedInstalled ) // no available hit, we need to print it
    {
      // show installed product in absence of an available one:
      if ( notinst_only || repofilter )
        continue;

      TableRow tr;
      bool isLocked = installed.status().isLocked();
      tr << lockStatusTag( "i", isLocked, installed.identIsAutoInstalled() );
      add_product_table_row( zypper, tr, installed->asKind<Product>() );
      tbl << tr;
    }
  }
  tbl.sort(1); // Name

  if ( tbl.empty() )
    zypper.out().info(_("No products found.") );
  else
    // display the result, even if --quiet specified
    cout << tbl;
}

void list_products( Zypper & zypper )
{
  if ( zypper.out().type() == Out::TYPE_XML )
    list_products_xml( zypper );
  else
    list_product_table( zypper );
}

// list_what_provides() isn't called any longer, ZypperCommand::WHAT_PROVIDES_e is
// replaced by Zypper::SEARCH_e with appropriate options (see Zypper.cc, line 919)
void list_what_provides( Zypper & zypper, const std::string & str )
{
  Capability cap( Capability::guessPackageSpec( str) );
  sat::WhatProvides q( cap );

  // is there a provider for the requested capability?
  if ( q.empty() )
  {
    zypper.out().info( str::form(_("No providers of '%s' found."), str.c_str()) );
    return;
  }

  // Ugly task of sorting the query (would be cool if table would do this):
  // 1st group by name
  std::map<IdString, std::vector<sat::Solvable>> res;
  for_( it, q.solvableBegin(), q.solvableEnd() )
  {
    res[(*it).ident()].push_back( *it );
  }
  // 2nd follow picklist (available items list prepended by those installed items not identicalAvailable)
  Table t;
  FillSearchTableSolvable fsts(t);
  for_( nameit, res.begin(), res.end() )
  {
    const ui::Selectable::Ptr sel( ui::Selectable::get( nameit->first ) );
    // replace installed by identical available if exists
    std::set<PoolItem> piset;
    for_( solvit, nameit->second.begin(), nameit->second.end() )
    {
      PoolItem pi( *solvit );
      if ( pi->isSystem() )
      {
	PoolItem identical( sel->identicalAvailableObj( pi ) );
	piset.insert( identical ? identical : pi );
      }
      else
	piset.insert( pi );
    }
    // follow picklist and print the ones we found
    for_( it, sel->picklistBegin(), sel->picklistEnd() )
    {
      if ( piset.find( *it ) != piset.end() )
	fsts.addPicklistItem( sel, *it );
    }
  }
  cout << t;
}

// Local Variables:
// c-basic-offset: 2
// End:
