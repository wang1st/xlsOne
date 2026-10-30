export function billingReady(billing) {
  return Boolean(billing?.enabled === true
    && /^live_[a-zA-Z0-9]+$/.test(billing.clientToken || '')
    && ['monthly', 'yearly'].every(period => /^pri_[a-zA-Z0-9]+$/.test(billing[period]?.priceId || '')
      && typeof billing[period]?.displayPrice === 'string' && billing[period].displayPrice.trim()));
}

export function verifiedDownloads(downloads) {
  return Array.isArray(downloads) ? downloads.filter(item => {
    try {
      return item.verified === true && new URL(item.url).protocol === 'https:'
        && /^[a-f0-9]{64}$/i.test(item.sha256 || '') && typeof item.version === 'string'
        && Boolean(item.label) && Boolean(item.platform);
    } catch { return false; }
  }) : [];
}

export function selectedPlan(value) {
  return value === 'yearly' ? 'yearly' : 'monthly';
}
