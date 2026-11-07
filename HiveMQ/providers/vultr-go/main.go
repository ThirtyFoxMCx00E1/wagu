// MP 4.12 region deployer (Vultr, github.com/vultr/govultr): one program for Windows / Linux / Mac,
// no bash, curl or jq needed. Same result as deploy-vultr.sh: firewall group "mp-regions",
// one Ubuntu 24.04 VPS per region (Singapore, Frankfurt, Los Angeles, Sydney), DuckDNS names set.
//
//   mp-deploy-vultr                  all 4 (asks for the API key / DuckDNS token if not set)
//   mp-deploy-vultr asia oceania     some
//   mp-deploy-vultr --list           your MP VPSes + IPs
//   env: VULTR_API_KEY, DUCKDNS_TOKEN, PLAN (default vc2-1c-2gb), OS_ID (default 2284 = Ubuntu 24.04)
package main

import (
	"bufio"
	"context"
	_ "embed"
	"encoding/base64"
	"fmt"
	"io"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/vultr/govultr/v3"
)

//go:embed user-data.sh
var userData string

type region struct{ name, city, duck string }

var regions = []region{
	{"asia", "sgp", "mpsamp"}, {"europe", "fra", "eusamp"}, {"na", "lax", "nasamp"}, {"oceania", "syd", "ocsamp"},
}

var ports = []string{"tcp:22", "tcp:80", "tcp:1883", "tcp:8883", "tcp:8884", "tcp:7681",
	"udp:1922", "udp:1930", "udp:1931", "udp:1932", "udp:7777", "udp:7780"}

type bearer struct{ key string }

func (b bearer) RoundTrip(r *http.Request) (*http.Response, error) {
	r = r.Clone(r.Context())
	if b.key != "" {
		r.Header.Set("Authorization", "Bearer "+b.key)
	}
	return http.DefaultTransport.RoundTrip(r)
}

var in = bufio.NewReader(os.Stdin)

func ask(env, prompt string) string {
	if v := strings.TrimSpace(os.Getenv(env)); v != "" {
		return v
	}
	fmt.Print(prompt)
	s, _ := in.ReadString('\n')
	return strings.TrimSpace(s)
}

func pause() {
	if len(os.Args) == 1 { // double-clicked on Windows: keep the window open
		fmt.Print("\nPress Enter to close...")
		in.ReadString('\n')
	}
}

func fail(f string, a ...any) { fmt.Printf(f+"\n", a...); pause(); os.Exit(1) }

func env(k, d string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return d
}

func main() {
	ctx := context.Background()
	key := ""
	if os.Getenv("MP_CRED_PROXY") != "1" {
		key = ask("VULTR_API_KEY", "Vultr API key (my.vultr.com > Account > API): ")
		if key == "" {
			fail("no API key")
		}
	}
	vc := govultr.NewClient(&http.Client{Transport: bearer{key}, Timeout: 60 * time.Second})

	if len(os.Args) > 1 && os.Args[1] == "--list" {
		list, _, _, err := vc.Instance.List(ctx, &govultr.ListOptions{PerPage: 100})
		if err != nil {
			fail("list: %v", err)
		}
		for _, i := range list {
			if strings.HasPrefix(i.Label, "mp-") {
				fmt.Printf("%s\t%s\t%s\t%s/%s\n", i.Label, i.Region, i.MainIP, i.Status, i.ServerStatus)
			}
		}
		return
	}

	duck := ""
	if os.Getenv("MP_CRED_PROXY") != "1" {
		duck = ask("DUCKDNS_TOKEN", "DuckDNS token (Enter = skip, set the names yourself): ")
	}
	plan := env("PLAN", "vc2-1c-2gb")
	osID, _ := strconv.Atoi(env("OS_ID", "2284"))

	want := regions
	if len(os.Args) > 1 {
		want = nil
		for _, a := range os.Args[1:] {
			ok := false
			for _, r := range regions {
				if r.name == a {
					want, ok = append(want, r), true
				}
			}
			if !ok {
				fmt.Printf("unknown region %s (asia europe na oceania)\n", a)
			}
		}
	}

	// firewall group mp-regions (made once, shared by all 4)
	fw := ""
	groups, _, _, err := vc.FirewallGroup.List(ctx, &govultr.ListOptions{PerPage: 100})
	if err != nil {
		fail("Vultr API: %v\n(check the key, and Account > API > Access Control: allow your IP)", err)
	}
	for _, g := range groups {
		if g.Description == "mp-regions" {
			fw = g.ID
		}
	}
	if fw == "" {
		g, _, err := vc.FirewallGroup.Create(ctx, &govultr.FirewallGroupReq{Description: "mp-regions"})
		if err != nil {
			fail("firewall: %v", err)
		}
		fw = g.ID
		for _, p := range ports {
			pr, port, _ := strings.Cut(p, ":")
			if _, _, err := vc.FirewallRule.Create(ctx, fw, &govultr.FirewallRuleReq{IPType: "v4", Protocol: pr,
				Subnet: "0.0.0.0", SubnetSize: 0, Port: port, Notes: "mp"}); err != nil {
				fmt.Printf("firewall rule %s: %v\n", p, err)
			}
		}
		fmt.Println("firewall group mp-regions:", fw)
	}

	for _, r := range want {
		ud := strings.NewReplacer("__REGION__", r.name, "__DUCKDNS__", duck).Replace(userData)
		inst, _, err := vc.Instance.Create(ctx, &govultr.InstanceCreateReq{Region: r.city, Plan: plan, OsID: osID,
			Label: "mp-" + r.name, Hostname: "mp-" + r.name, FirewallGroupID: fw, Backups: "disabled",
			UserData: base64.StdEncoding.EncodeToString([]byte(ud))})
		if err != nil {
			fmt.Printf("mp-%s: %v\n", r.name, err)
			continue
		}
		fmt.Printf("mp-%s: created in %s (%s), waiting for its IP...\n", r.name, r.city, inst.ID)
		ip := ""
		for i := 0; i < 60 && (ip == "" || ip == "0.0.0.0"); i++ {
			time.Sleep(5 * time.Second)
			if g, _, err := vc.Instance.Get(ctx, inst.ID); err == nil {
				ip = g.MainIP
			}
		}
		if (duck != "" || os.Getenv("MP_CRED_PROXY") == "1") && ip != "" && ip != "0.0.0.0" {
			u := "https://www.duckdns.org/update?domains=" + r.duck + "&ip=" + ip
			if duck != "" {
				u += "&token=" + duck
			}
			res := "failed"
			if resp, err := http.Get(u); err == nil {
				b, _ := io.ReadAll(resp.Body)
				resp.Body.Close()
				res = string(b)
			}
			fmt.Printf("duckdns %s.duckdns.org -> %s: %s\n", r.duck, ip, res)
		}
		fmt.Printf("mp-%s: %s  (setup runs by itself, about 5 min: ssh root@%s tail -f /var/log/mp-firstboot.log)\n", r.name, ip, ip)
	}
	if duck == "" {
		fmt.Println("\nWithout a DuckDNS token: point mpsamp / eusamp / nasamp / ocsamp.duckdns.org at the IPs above.")
	}
	fmt.Println("Monthly cost: about $10 per VPS on vc2-1c-2gb. Delete them in my.vultr.com to stop billing.")
	pause()
}
