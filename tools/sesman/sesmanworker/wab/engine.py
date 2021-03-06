#!/usr/bin/python
# -*- coding: utf-8 -*-
DEFAULT_CONF_DIR = "/var/wab/etc/"
DEFAULT_SPEC_DIR = "/opt/wab/share/conf/"
from logger import Logger
try:
    from wabengine.common.exception import AuthenticationFailed
    from wabengine.common.exception import AuthenticationChallenged
    from wabengine.common.exception import LicenseLimitReached
    from wabengine.common.exception import MustChangePassword
    from wallixgenericnotifier import Notify, CX_EQUIPMENT, PATTERN_FOUND, \
        PRIMARY_CX_FAILED, SECONDARY_CX_FAILED, NEW_FINGERPRINT, WRONG_FINGERPRINT, \
        RDP_PATTERN_FOUND, FILESYSTEM_FULL
    from wallixgenericnotifier import LICENCE_EXPIRED, LICENCE_PRIMARY_CX_ERROR, \
        LICENCE_SECONDARY_CX_ERROR
    from wabconfig import Config
    from wabengine.client.sync_client import SynClient
    from wabengine.common.const import APPROVAL_ACCEPTED, APPROVAL_REJECTED, \
        APPROVAL_PENDING, APPROVAL_NONE
    from wabengine.common.const import APPREQ_REQUIRED, APPREQ_OPTIONAL
    from wabx509 import AuthX509
    WAB_BANNER_LOGO = """
         __      __         __   __   __
        /  \    /  \_____  |  | |  | |__|__  ___
        \   \/\/   /\__  \ |  | |  | |  \  \/  /
         \        /  / __ \|  |_|  |_|  |>    <
          \__/\  /  (____  /____/____/__/__/\_ \\
               \/        \/                   \/
            %s
        """
except Exception, e:
    import traceback
    tracelog = traceback.format_exc(e)
    try:
        from fake.proxyengine import *
        Logger().info("================================")
        Logger().info("==== Load Fake PROXY ENGINE ====")
        Logger().info("================================")
    except Exception, e:
        Logger().info(">>>>>> %s" % tracelog)

import time
import socket

MAGIC_AM = u"__WAB_AM__"

def _binary_ip(network, bits):
    # TODO need Ipv6 support
    # This is a bit too resilient, add check for obviously bad values
    a,b,c,d = [ int(x)&0xFF for x in network.split('.')]
    mask = (0xFFFFFFFF >> bits) ^ 0xFFFFFFFF
    return ((a << 24) + (b << 16) + (c << 8) + d) & mask

def is_device_in_subnet(device, subnet):
    if subnet is None:
        return False
    if '/' in subnet:
        try:
            network, bits = subnet.rsplit('/')
            network_bits = _binary_ip(network, int(bits))
            device_bits = _binary_ip(device, int(bits))
            result = network_bits == device_bits
        except Exception, e:
            Logger().error("Bad host definition device '%s' subnet '%s': %s" % (device, subnet, str(e)))
            result = False
    else:
        result = device == subnet
    Logger().debug("checking if device %s is in subnet %s -> %s" % (device, subnet, ['No', 'Yes'][result]))
    return result

def read_config_file(modulename="sesman",
                     confdir=DEFAULT_CONF_DIR,
                     specdir=DEFAULT_SPEC_DIR):
    return Config(modulename=modulename, confdir=confdir, specdir=specdir)

def parse_auth(username):
    """
    Extract actual username and target if provided
    from authentication identity

    string format is <secondaryuser>@<target>:<service>:<group>:<primaryuser>
    always return primaryuser and either secondary target or None

    Note: primary user can be a path instead when this function
    is called to parse scp or sftp arguments.

    Because of compatibility issues with some ssh command line tools
    '+' can be used instead of ':'

    fields can be missing (typically service and group if there is no ambiguity)

    """
    user_at_dev_service_group, sep, primary = username.rpartition(':')
    if not sep:
        user_at_dev_service_group, sep, primary = username.rpartition('+')
    if sep:
        user_at_dev_service, sep, group = user_at_dev_service_group.rpartition(sep)
        if not sep:
            # service and group not provided
            user_at_dev, service, group = user_at_dev_service_group, '', ''
        else:
            user_at_dev, sep, service = user_at_dev_service.rpartition(sep)
            if not sep:
                # group not provided
                user_at_dev, service, group = user_at_dev_service, group, ''
        user, sep, dev = user_at_dev.rpartition('@')
        if sep:
            return primary, (user, dev, service, group)
    return username, None


class Engine(object):
    def __init__(self):
        self.wabengine = None
        self.wabuser = None
        self.client = SynClient('localhost', 'tcp:8803')
        self.session_id = None
        self.auth_x509 = None
        self._trace_encryption = None            # local ?
        self.challenge = None
        self.session_record = None
        self.deconnection_epoch = 0xffffffff
        self.deconnection_time = u"-"
        self.erpm_password_checked_out = False

        self.proxy_rights = None
        self.rights = None
        self.targets = {}
        self.target_right = None
        self.physical_targets = []
        self.displaytargets = []
        self.proxyrightsinput = None
        self.pidhandler = None

        self.session_result = True
        self.session_diag = u'Success'
        self.primary_password = None

    def set_session_status(self, result=None, diag=None):
        # Logger().info("Engine set session status : result='%s', diag='%s'" %
        #               (result, diag))
        if result is not None:
            self.session_result = result
        if diag is not None:
            self.session_diag = diag

    def get_language(self):
        try:
            if self.wabuser:
                return self.wabuser.preferredLanguage
            return self.wabengine.who_am_i().preferredLanguage
        except Exception, e:
            return 'en'

    def get_username(self):
        try:
            if self.wabuser.is_ldap:
                return "%s@%s" % (self.wabuser.cn, self.wabuser.ldap_map.ldapDomain)
            else:
                return self.wabuser.cn
        except Exception, e:
            return ""

    def get_force_change_password(self):
        try:
            return self.wabuser.forceChangePwd
        except Exception, e:
            return False

    def init_timeframe(self, auth):
        if (auth.deconnection_time
            and auth.deconnection_time != u"-"
            and auth.deconnection_time[0:4] <= u"2034"):
            self.deconnection_time = auth.deconnection_time
            self.deconnection_epoch = int(
                time.mktime(
                    time.strptime(
                        auth.deconnection_time,
                        "%Y-%m-%d %H:%M:%S"
                    )
                )
            )

    def get_trace_encryption(self):
        try:
            conf = Config("wabengine")
            self._trace_encryption = True if conf['trace'] == u'cryptofile' else False # u'localfile'
            return self._trace_encryption
        except Exception, e:
            import traceback
            Logger().info("Engine get_trace_encryption failed: configuration file section 'wabengine', key 'trace', (((%s)))" % traceback.format_exc(e))
        return False

    def password_expiration_date(self):
        try:
            _data = self.wabengine.check_password_expiration_info()
            if _data[2]:
                Logger().info("Engine password_expiration_date=%s" % _data[0])
                return True, _data[0]
        except Exception, e:
            import traceback
            Logger().info("Engine password_expiration_date failed: (((%s)))" % traceback.format_exc(e))
        return False, 0

    def is_x509_connected(self, wab_login, ip_client, proxy_type, target, server_ip):
        """
        Ask if we are authentifying using x509
        (and ask user by opening confirmation popup if we are,
        session ticket will be asked later in x509_authenticate)
        """
        try:
            self.auth_x509 = AuthX509(username = wab_login,
                                      ip = ip_client,
                                      requestor = proxy_type,
                                      target = target,
                                      server_ip = server_ip)
            result = self.auth_x509.is_connected()
            return result
        except Exception, e:
            import traceback
            Logger().info("Engine is_x509_connected failed: (((%s)))" % traceback.format_exc(e))
        return False

    def x509_authenticate(self):
        try:
            self.wabengine = self.auth_x509.get_proxy()
            if self.wabengine is not None:
                self.wabuser = self.wabengine.who_am_i()
                self.primary_password = None
                return True
        except AuthenticationFailed, e:
            pass
        except LicenseLimitReached, e:
            self.challenge = None
        except Exception, e:
            import traceback
            Logger().info("Engine x509_authenticate failed: (((%s)))" % traceback.format_exc(e))
        return False

    def password_authenticate(self, wab_login, ip_client, password, server_ip):
        try:
            self.wabengine = self.client.authenticate(username = wab_login,
                                                      password = password,
                                                      ip_source = ip_client,
                                                      challenge = self.challenge,
                                                      server_ip = server_ip)
            self.challenge = None
            if self.wabengine is not None:
                self.wabuser = self.wabengine.who_am_i()
                self.primary_password = password
                return True
        except AuthenticationChallenged, e:
            self.challenge = e.challenge
        except AuthenticationFailed, e:
            self.challenge = None
        except LicenseLimitReached, e:
            self.challenge = None
        except Exception, e:
            self.challenge = None
            import traceback
            Logger().info("Engine password_authenticate failed: (((%s)))" % traceback.format_exc(e))
        return False

    def passthrough_authenticate(self, wab_login, ip_client, server_ip):
        try:
            self.wabengine = self.client.authenticate_gssapi(username = wab_login,
                                                             realm = "realm",
                                                             ip_source = ip_client,
                                                             server_ip = server_ip)
            if self.wabengine is not None:
                self.wabuser = self.wabengine.who_am_i()
                self.primary_password = None
                return True
        except AuthenticationFailed, e:
            self.challenge = None
        except LicenseLimitReached, e:
            self.challenge = None
        except Exception, e:
            import traceback
            Logger().info("Engine passthrough_authenticate failed: (((%s)))" % traceback.format_exc(e))
        return False

    def resolve_target_host(self, target_device, target_login, target_service,
                            target_group, real_target_device, target_context,
                            passthrough_mode, protocols):
        """ Resolve the right target host to use
        target_context.host will contains the target host.
        target_context.showname() will contains the target_device to show
        target_context.login will contains the target_login if not in
            passthrough mode.

        Returns None if target_device is a hostname,
                target_device in other cases
        """
        if real_target_device:
            # Transparent proxy
            if not target_context:
                target_context = TargetContext(host=real_target_device)
        elif target_device:
            # This allow proxy to check if target_device is a device_name
            # or a hostname.
            # In case it is a hostname, we keep the target_login as a filter.
            valid = self.valid_device_name(protocols, target_device)
            Logger().info("Check Valid device '%s' : res = %s" %
                          (target_device, valid))
            if not valid:
                # target_device might be a hostname
                try:
                    login_filter, service_filter, group_filter = None, None, None
                    dnsname = None
                    if (target_login and not passthrough_mode):
                        login_filter = target_login
                    if (target_service and not passthrough_mode):
                        service_filter = target_service
                    if (target_group and not passthrough_mode):
                        group_filter = target_group
                    host_ip = socket.getaddrinfo(target_device, None)[0][4][0]
                    Logger().info("Resolve DNS Hostname %s -> %s" % (target_device,
                                                                     host_ip))
                    try:
                        socket.inet_pton(socket.AF_INET, target_device)
                    except socket.error:
                        dnsname = target_device
                    target_context = TargetContext(host=host_ip,
                                                   dnsname=dnsname,
                                                   login=login_filter,
                                                   service=service_filter,
                                                   group=group_filter,
                                                   show=target_device)
                    target_device = None
                except Exception, e:
                    # import traceback
                    # Logger().info(">>>%s" %traceback.format_exc(e))
                    Logger().info("target_device is not a hostname")
        return target_device, target_context

    def get_license_status(self):
        u""" Three checks : expiration, primary limits, secondary limit
        If at least one fails, user can't connect at all to any device,
        but all three checks are performed wether one as
        yet failed or not to send all relevant notifications to ADMIN.
        """
        license_ok = True
        try:
            lic_status = self.wabengine.get_license_status()

            if lic_status.is_expired():
                Logger().info("LICENCE_EXPIRED")
                Notify(self.wabengine, LICENCE_EXPIRED, u"")
                license_ok = False
            if lic_status.is_secondary_limit_reached():
                Logger().info("SECONDARY LICENCE LIMIT")
                Notify(self.wabengine, LICENCE_SECONDARY_CX_ERROR, {u'nbSecondaryConnection': lic_status.secondary[0]})
                license_ok = False
        except Exception, e:
            """If calling get_license_status raise some error, user will be rejected as per invalid license"""
            import traceback
            Logger().info("Engine get_license_status failed: (((%s)))" % (traceback.format_exc(e)))
            license_ok = False

        return license_ok

    def NotifyConnectionToCriticalEquipment(self, protocol, user, source,
                                            ip_source, login, device, ip,
                                            time, url):
        try:
            notif_data = {
                u'protocol': protocol,
                u'user': user,
                u'source': source,
                u'ip_source': ip_source,
                u'login': login,
                u'device': device,
                u'ip': ip,
                u'time': time
                }

            if not (url is None):
                notif_data[u'url'] = url

            Notify(self.wabengine, CX_EQUIPMENT, notif_data)
        except Exception, e:
            import traceback
            Logger().info("Engine NotifyConnectionToCriticalEquipment failed: (((%s)))" % (traceback.format_exc(e)))

    def NotifyPrimaryConnectionFailed(self, user, ip):
        try:
            notif_data = {
                u'user': user,
                u'ip': ip
                }

            Notify(self.wabengine, PRIMARY_CX_FAILED, notif_data)
        except Exception, e:
            import traceback
            Logger().info("Engine NotifyPrimaryConnectionFailed failed: (((%s)))" % (traceback.format_exc(e)))

    def NotifySecondaryConnectionFailed(self, user, ip, account, device):
        try:
            notif_data = {
                   u'user'   : user
                 , u'ip'     : ip
                 , u'account': account
                 , u'device' : device
             }

            Notify(self.wabengine, SECONDARY_CX_FAILED, notif_data)
        except Exception, e:
            import traceback
            Logger().info("Engine NotifySecondaryConnectionFailed failed: (((%s)))" % (traceback.format_exc(e)))

    def NotifyFilesystemIsFullOrUsedAtXPercent(self, filesystem, used):
        try:
            notif_data = {
                u'filesystem': filesystem,
                u'used': used
                }

            Notify(self.wabengine, FILESYSTEM_FULL, notif_data)
        except Exception, e:
            import traceback
            Logger().info("Engine NotifyFilesystemIsFullOrUsedAtXPercent failed: (((%s)))" % (traceback.format_exc(e)))

    def NotifyFindPatternInRDPFlow(self, regexp, string, user_login, user, host, cn, service):
        try:
            notif_data = {
                   u'regexp'     : regexp
                 , u'string'     : string
                 , u'user_login' : user_login
                 , u'user'       : user
                 , u'host'       : host
                 , u'device'     : cn
                 , u'service'    : service
             }

            Notify(self.wabengine, RDP_PATTERN_FOUND, notif_data)
        except Exception, e:
            import traceback
            Logger().info("Engine NotifyFindPatternInRDPFlow failed: (((%s)))" % (traceback.format_exc(e)))

    def get_targets_list(self, group_filter, device_filter, protocol_filter, case_sensitive):
        targets = []
        item_filtered = False
        for target_info in self.displaytargets:
            temp_service_login                = target_info.service_login
            temp_resource_service_protocol_cn = target_info.protocol
            if not target_info.protocol == u"APP":
                if (target_info.target_name == u'autotest' or
                    target_info.target_name == u'bouncer2' or
                    target_info.target_name == u'widget2_message' or
                    target_info.target_name == u'widgettest' or
                    target_info.target_name == u'test_card'):
                    temp_service_login = target_info.service_login.replace(u':RDP',
                                                                           u':INTERNAL', 1)
                    temp_resource_service_protocol_cn = 'INTERNAL'

            compare_group         = target_info.group if case_sensitive else target_info.group.decode("utf-8").lower()
            compare_service_login = temp_service_login if case_sensitive else temp_service_login.decode("utf-8").lower()
            compare_protocol      = temp_resource_service_protocol_cn if case_sensitive else temp_resource_service_protocol_cn.decode("utf-8").lower()

            compare_filter_group = group_filter if case_sensitive else group_filter.decode("utf-8").lower()
            compare_filter_device = device_filter if case_sensitive else device_filter.decode("utf-8").lower()
            compare_filter_protocol= protocol_filter if case_sensitive else protocol_filter.decode("utf-8").lower()

            if ((compare_group.find(compare_filter_group) == -1)
                or (compare_service_login.find(compare_filter_device) == -1)
                or (compare_protocol.find(compare_filter_protocol) == -1)):
                item_filtered = True
                continue

            targets.append((target_info.group # ( = concatenated list)
                            , temp_service_login
                            , temp_resource_service_protocol_cn
                            )
                           )
        # Logger().info("targets list = %s'" % targets)
        return targets, item_filtered

    def reset_proxy_rights(self):
        self.proxy_rights = None
        self.rights = None
        self.target_right = None

    def valid_device_name(self, protocols, target_device):
        # Logger().info("VALID DEVICE NAME target_device = '%s'" % target_device)
        prights = self.wabengine.get_proxy_rights(protocols, target_device,
                                                  check_timeframes=False)
        rights = prights.rights
        # Logger().info("VALID DEVICE NAME Rights = '%s', len = %s" % (rights, len(rights)))
        if rights:
            return True
        return False

    def get_proxy_rights(self, protocols, target_device=None, check_timeframes=False,
                         target_context=None):
        if self.proxy_rights is not None:
            return
        self.proxy_rights = self.wabengine.get_proxy_rights(protocols, target_device,
                                                            check_timeframes=check_timeframes)
        self.rights = self.proxy_rights.rights
        self.targets = {}
        self.displaytargets = []
        for right in self.rights:
            if right.resource and right.account:
                target_login = self.get_account_login(right)
                target_groups = [x.cn for x in right.group_targets]
                if right.resource.application:
                    target_name = right.resource.application.cn
                    service_name = u"APP"
                    protocol = u"APP"
                    host = None
                    alias = None
                else:
                    target_name = right.resource.device.cn
                    service_name = right.resource.service.cn
                    protocol = right.resource.service.protocol.cn
                    host = right.resource.device.host
                    alias = right.resource.device.deviceAlias
                if target_context is not None:
                    if host is None:
                        continue
                    if (target_context.host and
                        not is_device_in_subnet(target_context.host, host) and
                        host != target_context.dnsname):
                        continue
                    if (target_context.login and
                        target_login != target_context.login):
                        continue
                    if (target_context.service and
                        service_name != target_context.service):
                        continue
                    if (target_context.group and
                        not (target_context.group in target_groups)):
                        continue
                tuple_index = (target_login, target_name)
                if not self.targets.get(tuple_index):
                    self.targets[tuple_index] = []
                self.targets[tuple_index].append((service_name, target_groups, right))
                if alias:
                    alias_index = (target_login, alias)
                    if not self.targets.get(alias_index):
                        self.targets[alias_index] = []
                    self.targets[alias_index].append((service_name, target_groups, right))
                self.displaytargets.append(DisplayInfo(target_login,
                                                       target_name,
                                                       service_name,
                                                       protocol,
                                                       target_groups,
                                                       right.subprotocols,
                                                       host))

    def get_selected_target(self, target_login, target_device, target_service,
                            target_group):
        # Logger().info(">>==GET_SELECTED_TARGET %s@%s:%s:%s" % (target_device, target_login, target_service, target_group))
        right = None
        self.get_proxy_rights([u'RDP', u'VNC'], target_device,
                              check_timeframes=False)
        if target_login == MAGIC_AM:
            target_login = self.get_username()
        results = self.targets.get((target_login, target_device))
        right = None
        filtered = []
        for (r_service, r_groups, r) in results:
            if target_service and not (r_service == target_service):
                continue
            if target_group and not (target_group in r_groups):
                continue
            filtered.append((r_service, r))
        if filtered:
            filtered_service, right = filtered[0]
            # if ambiguity in group but not in service,
            # get the right without approval
            for (r_service, r) in filtered[1:]:
                if filtered_service != r_service:
                    right = None
                    break
                if r.authorization.hasApproval is False:
                    right = r
        if right:
            self.init_timeframe(right)
            self.target_right = right
        return right

    def get_effective_target(self, selected_target):
        Logger().info("Engine get_effective_target: service_login=%s" % selected_target.service_login)
        try:
            if selected_target.resource.application:
                effective_target = self.wabengine.get_effective_target(selected_target)
                Logger().info("Engine get_effective_target done (application)")
                return effective_target
            else:
                Logger().info("Engine get_effective_target done (physical)")
                return [selected_target]

        except Exception, e:
            import traceback
            Logger().info("Engine get_effective_target failed: (((%s)))" % (traceback.format_exc(e)))
        return []

    def get_app_params(self, selected_target, effective_target):
#         Logger().info("Engine get_app_params: service_login=%s effective_target=%s" % (service_login, effective_target))
        Logger().info("Engine get_app_params: service_login=%s" % selected_target.service_login)
        try:
            app_params = self.wabengine.get_app_params(selected_target, effective_target)
            Logger().info("Engine get_app_params done")
            return app_params
        except Exception, e:
            import traceback
            Logger().info("Engine get_app_params failed: (((%s)))" % (traceback.format_exc(e)))
        return None

    def get_target_password(self, target_device):
#         Logger().info("Engine get_target_password: target_device=%s" % target_device)
        Logger().debug("Engine get_target_password ...")
        if target_device.account.login == MAGIC_AM and self.primary_password:
            Logger().info("Account mapping: get primary password ...")
            return self.primary_password
        try:
            target_password = self.wabengine.get_target_password(target_device)
            self.erpm_password_checked_out = True
            if not target_password:
                target_password = u''
            Logger().debug("Engine get_target_password done")
            return target_password
        except Exception, e:
            import traceback
            Logger().info("Engine get_target_password failed: (((%s)))" % (traceback.format_exc(e)))
        return u''

    def release_target_password(self, target_device, reason, target_application = None):
        Logger().debug("Engine release_target_password: reason=\"%s\"" % reason)
        if target_device == target_application:
            target_application = None
        self.erpm_password_checked_out = False
        try:
            self.wabengine.release_target_password(target_device, reason, target_application)
            Logger().debug("Engine release_target_password done")
        except Exception, e:
            import traceback
            Logger().info("Engine release_target_password failed: (((%s)))" % (traceback.format_exc(e)))

    def get_pidhandler(self, pid):
        if not self.pidhandler:
            try:
                from wabengine.common.interface import IPBSessionHandler
                from wabengine.common.utils import ProcessSessionHandler
                self.pidhandler = IPBSessionHandler(ProcessSessionHandler(int(pid)))
            except Exception, e:
                self.pidhandler = None
        return self.pidhandler

    def start_session(self, auth, pid, effective_login=None, **kwargs):
        if auth.account.login == MAGIC_AM and not effective_login:
            effective_login = self.get_username()
        self.session_id = self.wabengine.start_session(auth, self.get_pidhandler(pid),
                                                       effective_login=effective_login,
                                                       **kwargs)
        return self.session_id

    def update_session(self, physical_target, **kwargs):
        """Update current session with target name.

        :param target physical_target: selected target
        :return: None
        """
        hosttarget = u"%s@%s:%s" % (
            self.get_account_login(physical_target),
            physical_target.resource.device.cn,
            physical_target.resource.service.cn)
        try:
            if self.session_id:
                self.wabengine.update_session(self.session_id, hosttarget, **kwargs)
        except Exception, e:
            import traceback
            Logger().info("Engine update_session failed: (((%s)))" % (traceback.format_exc(e)))

    def stop_session(self, title=u"End session"):
        try:
            if self.session_id:
                # Logger().info("Engine stop_session: result='%s', diag='%s', title='%s'" %
                #               (self.session_result, self.session_diag, title))
                self.wabengine.stop_session(self.session_id, result=self.session_result,
                                            diag=self.session_diag, title=title)
        except Exception, e:
            import traceback
            Logger().info("Engine stop_session failed: (((%s)))" % (traceback.format_exc(e)))

    def get_restrictions(self, auth, proxytype):
        if proxytype == "RDP":
            separator = u"\x01"
            matchproto = lambda x: x == u"RDP"
        elif proxytype == "SSH":
            separator = u"|"
            matchproto = lambda x: (x == self.subprotocol
                                    or (x == u'SSH_SHELL_SESSION'
                                        and self.subprotocol == u'SSH_X11_SESSION'))
        else:
            return None, None
        try:
            restrictions = self.wabengine.get_proxy_restrictions(auth)
            kill_patterns = []
            notify_patterns = []
            for restriction in restrictions:
                if not restriction.subprotocol:
                    Logger().error("No subprotocol in restriction!")
                    continue
                if matchproto(restriction.subprotocol.cn):
                    Logger().debug("adding restriction %s %s %s" % (restriction.action, restriction.data, restriction.subprotocol.cn))
                    if restriction.action == 'kill':
                        kill_patterns.append(restriction.data)
                    elif restriction.action == 'notify':
                        notify_patterns.append(restriction.data)

            self.pattern_kill = separator.join(kill_patterns)
            self.pattern_notify = separator.join(notify_patterns)
            Logger().info("pattern_kill = [%s]" % (self.pattern_kill))
            Logger().info("pattern_notify = [%s]" % (self.pattern_notify))
        except Exception, e:
            self.pattern_kill = None
            self.pattern_notify = None
            import traceback
            Logger().info("Engine get_restrictions failed: (((%s)))" % (traceback.format_exc(e)))
        return (self.pattern_kill, self.pattern_notify)

    def write_trace(self, video_path):
        try:
            _status, _error = True, u"No error"
            if video_path:
                # Notify WabEngine with Trace file descriptor
                trace = self.wabengine.get_trace_writer(self.session_id, trace_type=u"rdptrc")
                trace.writeframe(str("%s.mwrm" % (video_path.encode('utf-8')) ) )
                trace.end()
        except Exception, e:
            Logger().info("Engine write_trace failed: %s" % e)
            _status, _error = False, u"Trace writer failed for %s"
        return _status, _error

    def read_session_parameters(self, key=None):
        return self.wabengine.read_session_parameters(self.session_id, key=key)

    def check_target(self, target, pid=None, request_ticket=None):
        status, infos = self.wabengine.check_target(target, self.get_pidhandler(pid),
                                                    request_ticket)
        # Logger().info("status : %s" % status)
        # Logger().info("infos : %s" % infos)
        deconnection_time = infos.get("deconnection_time")
        if deconnection_time:
            Logger().info("deconnection_time updated from %s to %s" % (target.deconnection_time, deconnection_time))
            target.deconnection_time = deconnection_time
            # update deconnection_time in right
        return status, infos

    def get_application(self, selected_target=None):
        target = selected_target or self.target_right
        if not target:
            return None
        return target.resource.application

    def get_target_protocols(self, selected_target=None):
        target = selected_target or self.target_right
        if not target:
            return None
        proto = target.resource.service.protocol.cn
        subproto = [x.cn for x in target.subprotocols]
        return ProtocolInfo(proto, subproto)

    def get_target_extra_info(self, selected_target=None):
        target = selected_target or self.target_right
        if not target:
            return None
        isRecorded = target.authorization.isRecorded
        isCritical = target.authorization.isCritical
        return ExtraInfo(isRecorded, isCritical)

    def get_deconnection_time(self, selected_target=None):
        target = selected_target or self.target_right
        if not target:
            return None
        return target.deconnection_time

    def get_target_agent_forwardable(self, selected_target=None):
        target = selected_target or self.target_right
        if not target:
            return None
        return target.account.isAgentForwardable

    def get_physical_target_info(self, physical_target):
        return PhysicalTarget(device_host=physical_target.resource.device.host,
                              account_login=self.get_account_login(physical_target),
                              service_port=int(physical_target.resource.service.port))

    def get_target_login_info(self, selected_target=None):
        target = selected_target or self.target_right
        if not target:
            return None
        if target.resource.application:
            target_name = target.resource.application.cn
            device_host = None,
        else:
            target_name = target.resource.device.cn
            device_host = target.resource.device.host
        account_login = self.get_account_login(target)
        service_port = target.resource.service.port
        service_name = target.resource.service.cn
        conn_cmd = target.resource.service.authmechanism.data
        autologon = target.account.password or target.account.isAgentForwardable
        return LoginInfo(account_login=account_login,
                         target_name=target_name,
                         service_name=service_name,
                         device_host=device_host,
                         service_port=service_port,
                         conn_cmd=conn_cmd,
                         autologon=autologon)

    def get_account_login(self, right):
        account_login = right.account.login
        if account_login == MAGIC_AM:
            account_login = self.get_username()
        return account_login

# Information Structs
class TargetContext(object):
    def __init__(self, host=None, dnsname=None, login=None, service=None,
                 group=None, show=None):
        self.host = host
        self.dnsname = dnsname
        self.login = login
        self.service = service
        self.group = group
        self.show = show
    def showname(self):
        return self.show or self.dnsname or self.host

class DisplayInfo(object):
    def __init__(self, target_login, target_name, service_name,
                 protocol, group, subproto, host):
        self.target_login = target_login
        self.target_name = target_name
        self.service_name = service_name
        self.protocol = protocol
        # self.group = ";".join([x.cn for x in group])
        self.group = ";".join(group)
        self.service_login = "%s@%s:%s" % (self.target_login, self.target_name, self.service_name)
        self.subprotocols = [x.cn for x in subproto] if subproto else []
        self.host = host

class ProtocolInfo(object):
    def __init__(self, protocol, subprotocols=[]):
        self.protocol = protocol
        self.subprotocols = subprotocols

class ExtraInfo(object):
    def __init__(self, is_recorded, is_critical):
        self.is_recorded = is_recorded
        self.is_critical = is_critical

class PhysicalTarget(object):
    def __init__(self, device_host, account_login, service_port):
        self.device_host = device_host
        self.account_login = account_login
        self.service_port = service_port

class LoginInfo(object):
    def __init__(self, account_login, target_name, service_name,
                 device_host, service_port, conn_cmd, autologon):
        self.account_login = account_login
        self.target_name = target_name
        self.service_name = service_name
        self.device_host = device_host
        self.service_port = service_port
        self.conn_cmd = conn_cmd
        self.autologon = autologon
